# Iris — Roadmap

Tasks are closed as they ship. No fixed versions — a version tag is cut
when a meaningful set of tasks lands together.

---

## Done

- [x] Core type registry with content-addressed TypeId (FNV-64)
- [x] IrisValue with OpaqueHandle — JNI-free core
- [x] JavaBackend: `c_to_java` / `java_to_c` round-trip over JNI
- [x] `TypeRegistry::inspect()` — static type info without any runtime call
- [x] `JavaBackend::dry_run()` — bridge analysis without touching the JVM
- [x] `TypeRegistry::freeze()` — lock registry post-init, zero-trust
- [x] OS utilities: `ls`, `ps`, `env` → typed `IrisValue` streams
- [x] OS commands return `expected<vector<IrisValue>, OsError>` on failure
- [x] `Channel` — thread-safe `IrisValue` queue connecting two backends
- [x] `emit` / `recv` wired to `Channel`
- [x] `pipe(c_val)` — `c_to_java` → Java method → `java_to_c` as one call
- [x] `OsBackend` satisfying the `Backend` concept (streaming `recv`)
- [x] `RuntimeManager` — one `JavaVM*` per process, thread-safe acquisition
- [x] `FnBackend<F>` — wrap any C++ callable as a `Backend`
- [x] `JavaBackend::invoke()` — call a static Java method with an `IrisValue`
- [x] `IrisBackendHandle` C ABI — vtable of C function pointers
- [x] `IpcBackend` — carries `IrisValue` over Unix socketpair / named socket
- [x] `sdk/iris_backend.h` (MIT) — C ABI interface decoupled from GPL core
- [x] SDK restructured by language: `sdk/*.h` (C ABI), `sdk/cpp/*.hpp` (C++), `sdk/py/*.py` (Python)
- [x] `sdk/iris_registry.h` (MIT) — C ABI for type registration; usable from Python/Rust/Go FFI
- [x] `sdk/cpp/iris.hpp` — C++ umbrella (types + macros + wrap/unwrap); IRIS_TYPE uses C ABI
- [x] `sdk/cpp/backend.hpp` — RAII C++ wrapper over C ABI, no GPL headers
- [x] `sdk/py/iris.py` — ctypes bindings for type registration; smoke-tested
- [x] `IrisBuffer` — zero-copy ref-counted buffer (`shared_ptr<byte[]>`); replaces `vector<byte>`
- [x] `IpcBackend::emit` — `writev` scatter-gather (header + payload in one syscall)
- [x] Reference worker `.so` — minimal plugin proves dlopen embed path end-to-end
- [x] JNI local-ref leak fixes in `register_class` (`jname`, `jtname`, `class_cls`, `field_cls`)
- [x] `c_to_java` Str marshalling — `const char*` in raw → `NewStringUTF` → `SetObjectField`
- [x] `FnBackend composition` — `fn_a | fn_b` chains backends via `operator|`
- [x] `libirisos` as a separate CMake target — `src/backend/os/` compiled independently;
      core `libiris` has no OS dependency; `IRIS_HAS_OS=1` propagates via INTERFACE

---

## Now

- [ ] IpcBackend zero-copy recv — map incoming payload into shared IrisBuffer without
      intermediate copy; zero alloc path for fixed-size types
- [x] `std::execution` sender adaptor — `iris::just(val) | iris::via(backend) | iris::then(f)`;
      `sync_wait()` synchronously, `schedule_on(thread_pool)` for async
- [ ] `std::meta` — derive `TypeDescriptor` without listing fields manually (C++26)
- [ ] Abstract OS layer: platform guards so `ls`/`ps` compile on macOS and Windows
- [ ] CI matrix: Linux, macOS, (Windows MinGW)

---

## Core VM — REPL

`FnBackend<F>` + `operator|` are the minimal building blocks. Goal: a standalone 2MB
runtime (no JVM, no LLVM) where typed structs flow through a pipeline of pure C++ functions.

- [ ] `iris_repl` — console reads user input, constructs `IrisValue` by matching
      `TypeRegistry` entries, feeds into any `Backend`; result printed as typed fields.
      Input syntax: `Point{x:1, y:2}` → `IrisValue(TypeId=Point, raw=[01 00 ... 02 00 ...])`
      This gives a 2MB scripting runtime that gains Java/WASM/IPC for free when those
      backends are linked in.
- [ ] Java utility set discovery — `sdk/java/` directory scanned at connect() time;
      `.jar` files auto-registered as `TypeDescriptor` sources; no hand-written IRIS_TYPE
      required for pure-Java projects

---

## Far

- [ ] `WasmBackend` — mirror of `JavaBackend` for wasmtime/wasmer; bridges
      `IrisValue` into WASM linear memory so plugins running in a WASM sandbox
      receive typed messages the same way subprocess workers do
- [ ] `RubyBackend` — load libruby, reflect `rb_cObject` fields into `TypeDescriptor`;
      same lifecycle pattern as `JavaBackend` (one VM per process via `RuntimeManager`);
      `c_to_ruby` / `ruby_to_c` round-trip; `invoke()` calls Ruby method by name
- [ ] Schema evolution — reject a worker whose `TypeDescriptor` layout has
      drifted from what the host registered; catches stale `.so` at load time
- [ ] FFM backend for Java 22+ (zero-copy `MemorySegment`)
- [ ] Rust bindings (`sdk/rs/`) — safe wrapper over C ABI via `bindgen`
- [ ] Go bindings (`sdk/go/`) — CGO wrapper for `iris_type_register` + backend vtable
