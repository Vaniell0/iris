# Iris — engine reference

Iris is the substrate. It solves one problem: moving typed values between
language runtimes without duplication of the type definition.

Register a type once. Every backend — JVM, IPC, OS, async pipeline — reads
the same `TypeDescriptor`. No JNI by hand. No schema file. No codegen.
No serialization library. The type you already wrote is the only source of truth.

---

## IrisValue — the universal value

Every value in Iris is an `IrisValue`: a `TypeId` plus one of three payloads.

```
IrisValue
├── IrisBuffer   — flat bytes, ref-counted, zero-copy; wire-safe; crosses IPC
├── OpaqueHandle — type-erased pointer (JVM global ref, WASM extern, etc.); process-local
└── string       — C++ heap string; process-local
```

**What "wire-safe" means:** only `IrisBuffer` payloads can cross a process
boundary via the IPC wire format. `OpaqueHandle` and `string` live in the
sender's heap — they are not transmitted.

For string data that must cross processes use `CStr` fields: null-terminated
chars stored inside the flat buffer, fully self-contained in the payload.

---

## TypeDescriptor

A `TypeDescriptor` describes one struct: its name, total size, and an ordered
list of `FieldDesc` values. Each `FieldDesc` carries:

| Field | Type | Meaning |
|-------|------|---------|
| `name` | string | field identifier |
| `kind` | PrimitiveKind | scalar type (see table below) |
| `offset` | size_t | byte offset inside the struct |
| `size` | size_t | byte size of the field |
| `jni_name` | string | optional override for Java field name |

### PrimitiveKind

| Constant | Value | C++ type | Wire |
|----------|-------|----------|------|
| Void | 0 | — | — |
| Bool | 1 | bool | 1 byte |
| I8 | 2 | int8_t | 1 byte |
| I16 | 3 | int16_t | 2 bytes LE |
| I32 | 4 | int32_t | 4 bytes LE |
| I64 | 5 | int64_t | 8 bytes LE |
| F32 | 6 | float | 4 bytes LE |
| F64 | 7 | double | 8 bytes LE |
| Str | 8 | char* | pointer — not wire-safe |
| Bytes | 9 | uint8_t[N] | N raw bytes |
| CStr | 10 | char[N] | N bytes, null-terminated — wire-safe string |

---

## TypeId — content-addressed identity

```cpp
TypeId = FNV-64(name || field_name₀ || kind₀ || size₀ || field_name₁ || ...)
```

Two binaries that define the same struct independently, without coordination,
will compute identical `TypeId`s. This is the same insight as COM GUIDs done
correctly: identity derived from content, not from a registry service.

---

## Registration paths

### Explicit — IRIS_TYPE (any C++ compiler, C++23)

```cpp
struct Point { int32_t x, y; };
IRIS_TYPE(Point,
    IRIS_FIELD(Point, x),
    IRIS_FIELD(Point, y)
)
// char[N] fields → IRIS_CSTR_FIELD  (null-terminated string in fixed buffer)
// uint8_t[N]    → IRIS_BYTES_FIELD  (raw binary blob)
```

Uses the C ABI (`iris_type_register`) — no GPL headers required in user code.

### Automatic — IRIS_REFLECT (GCC 16+, C++26, -freflection)

```cpp
struct Vec3 { float x, y, z; };
IRIS_REFLECT(Vec3)   // zero field list; derives everything via std::meta
```

`char[N]` fields are automatically mapped to `CStr`.
`uint8_t[N]` and other arrays fall back to `Bytes`.

### Dynamic — from_fields (runtime, used by irsh interpreter)

```cpp
TypeRegistry::global().from_fields("DirEntry", fields_vector, total_size);
```

Used by the irsh interpreter to register types discovered at runtime
(from `.jar` files, `.so` plugins, or type declarations in scripts).

### Java reflection — register_class (JavaBackend)

```cpp
JavaBackend::register_class(env, "com.example.Point");
```

Calls `getDeclaredFields()` via JNI; walks non-static fields; maps Java
primitive types to `PrimitiveKind`; calls `TypeRegistry::from_fields()`.
Java `String` fields → `PrimitiveKind::Str`.

---

## The Backend concept

Any type satisfying this concept is a backend:

```cpp
template<typename T> concept Backend = requires(T b, IrisValue v,
                                                const TypeDescriptor& td) {
    { b.runtime_name() } -> convertible_to<string_view>;
    { b.can_handle(td) } -> convertible_to<bool>;
    { b.emit(move(v)) }  -> same_as<void>;
    { b.recv() }         -> same_as<IrisValue>;
};
```

No base class, no vtable overhead. Verified at compile time via `static_assert`.

---

## Backends

### FnBackend — wrap any callable

```cpp
auto square = FnBackend([](IrisValue v) {
    auto p = iris::unwrap<Point>(v);
    p.x *= p.x; p.y *= p.y;
    return iris::wrap(p);
});

auto pipeline = square | FnBackend(print_fn);  // operator| chains backends
```

### IpcBackend — Unix socket transport

```cpp
// sender (irsh)
auto ipc = IpcBackend::connect("./worker.sock");
ipc.emit(iris::wrap(entry));

// receiver (any process)
auto ipc = IpcBackend::listen_and_accept("./worker.sock");
auto v   = ipc.recv();
```

`emit` uses `writev` scatter-gather (header + payload in one syscall, no copy).
See [PIPE.md](PIPE.md) for the full wire format specification.

### OsBackend — lazy OS command source

```cpp
auto backend = OsBackend::ls("/var/log");
while (true) {
    auto v = backend.recv();    // reads exactly one dirent per call
    if (!v.type_id) break;
    auto e = iris::unwrap<DirEntry>(v);
    // ...
}
```

`OsBackend` is a `std::variant<LsStream, PsStream, EnvStream>`.
Each stream uses the `OsStream<Derived, Entry>` CRTP pattern:
`open()` / `next()` / `close()` implemented by the derived class;
`~OsStream()` calls `close()` if opened; move constructors zero the source's
`opened_` flag to prevent double-close.

Eager wrappers for backward compatibility:
```cpp
auto entries = iris::os::ls("/var/log");   // expected<vector<IrisValue>, OsError>
auto procs   = iris::os::ps();
auto vars    = iris::os::env();
```

### JavaBackend — JVM bridge

```cpp
JavaBackend jb;
jb.connect("./plugins.jar");

auto java_val = jb.c_to_java(iris::wrap(point));   // C struct → Java object
auto back     = jb.java_to_c(*java_val, type_id);  // Java object → C struct

// call static Java method
auto result = jb.pipe(c_val, "transform");

// inspect without touching JVM
auto report = jb.dry_run(type_id);
// report.mappable_count — fields that have a JNI setter
// report.skipped_count  — Bytes/Void fields that cannot be mapped
```

One `JavaVM*` per process via `RuntimeManager`. Thread-safe `acquire()`.
CStr fields are marshalled as Java `String` via `NewStringUTF` / `GetStringUTFChars`.

---

## P2300 — async pipeline

```cpp
#include <execution.hpp>   // -DIRIS_STDEXEC=ON

auto result = iris::sync_wait(
    iris::just(iris::wrap(Point{3, 4}))
    | iris::via(fn_backend)
    | iris::then([](IrisValue v) { return v; })
);

// offload rest of chain to thread pool
iris::schedule_on(thread_pool, sender_chain);
```

Works with any type satisfying `Backend`.

---

## P2996 — automatic field reflection

```cpp
#include <sdk/cpp/reflect.hpp>   // -DIRIS_STDMETA=ON, GCC 16+, C++26, -freflection

struct Sensor { int32_t id; double reading; char label[32]; };
IRIS_REFLECT(Sensor)
// Sensor::label  → CStr  (char array, auto-detected)
// Sensor::id     → I32
// Sensor::reading → F64
```

---

## SDK layers

| Layer | Files | License | Who uses it |
|-------|-------|---------|-------------|
| C ABI | `sdk/iris_registry.h`, `sdk/iris_backend.h` | MIT | Python, Rust, Go, C |
| C++ | `sdk/cpp/iris.hpp`, `types.hpp`, `macros.hpp`, `backend.hpp`, `reflect.hpp` | MIT | C++ consumers, no GPL headers |
| Python | `sdk/py/iris.py` | MIT | Python scripts, irsh Python stage |
| Rust | `sdk/rs/` (planned) | MIT | Rust utilities for irsh pipeline |
| Go | `sdk/go/` (planned) | MIT | Go utilities for irsh pipeline |

The MIT SDK layer depends only on the C ABI — it does not require any GPL headers.

---

## Thread safety

| Component | Mechanism |
|-----------|-----------|
| TypeRegistry | `shared_mutex` (readers concurrent, writers exclusive) |
| `freeze()` | `atomic<bool>` release/acquire |
| Channel | `mutex` + `condition_variable` |
| RuntimeManager | `mutex` on acquire; `JavaVM*` immutable after first init |
| IrisBuffer | `shared_ptr<byte[]>` ref-count for zero-copy sharing |
| OpaqueHandle | RAII; release callback runs in the thread that destructs the value |

---

## Build options

| Flag | Default | Effect |
|------|---------|--------|
| `IRIS_JAVA_BACKEND` | ON | Compile JavaBackend + JNI bridge into `libiris.so` |
| `IRIS_OS_BACKEND` | ON | Compile `libirisos.so` (ls/ps/env); core `libiris` stays OS-free |
| `IRIS_STDEXEC` | OFF | Enable P2300 sender adaptors (requires nvidia/stdexec) |
| `IRIS_STDMETA` | OFF | Enable P2996 reflection (GCC 16+, C++26, -freflection) |

```bash
# full build
cmake -B build -GNinja \
  -DIRIS_JAVA_BACKEND=ON \
  -DIRIS_OS_BACKEND=ON  \
  -DIRIS_STDEXEC=ON     \
  -DIRIS_STDMETA=ON     # GCC 16+ only
cmake --build build

# minimal — no JVM, no OS backend, no reflection
cmake -B build -DIRIS_JAVA_BACKEND=OFF -DIRIS_OS_BACKEND=OFF
```

With Nix: `nix develop` drops into a GCC 16 shell with all dependencies.
