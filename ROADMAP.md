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
- [x] OS utilities: `ls`, `ps`, `env` → typed `IrisValue` streams (Apache 2.0)
- [x] SDK moved to `sdk/` (MIT) — type definitions and macros separate from core
- [x] OS commands return `expected<vector<IrisValue>, OsError>` on failure

---

## Now

- [x] `Channel` — thread-safe `IrisValue` queue connecting two backends
- [x] `emit` / `recv` wired to `Channel`
- [x] `pipe(c_val)` — `c_to_java` → Java method → `java_to_c` as one call
- [x] `OsBackend` satisfying the `Backend` concept (streaming `recv`)

---

## Cross-platform

- [ ] OS utilities as a separate `libirisos` — optional link, no GPL propagation concern
- [ ] Abstract OS layer: `#ifdef` / CMake platform guards so `ls`/`ps` compile on macOS and Windows
- [ ] `ProcEntry` fallback on non-Linux (macOS `sysctl`, Windows `EnumProcesses`)
- [ ] CI matrix: Linux, macOS, (Windows MinGW)

## Next — embeddable core

The concrete target: a subprocess worker in a `dlopen`-based plugin system
sends `IrisValue` over a wire protocol instead of raw CBOR blobs. The host
gets a `TypeDescriptor` with every message — no schema file, no hand-written
JNI to reach a Java backend, no deserializing to know what arrived.

For this to work Iris must be loadable by the host via `dlopen` with no C++
ABI dependency and no assumption about who owns the JVM.

- [ ] `IrisBackendHandle` C ABI — vtable of C function pointers
      (`iris_backend_connect / emit / recv / disconnect`); the host calls
      through it without linking against Iris headers
- [ ] Reference worker `.so` — minimal plugin that registers types, receives
      `IrisValue` from the host, returns a result; proves the embed path works
      end-to-end
- [ ] `RuntimeManager` — one `JavaVM*` per process, thread-safe acquisition;
      the host process may already have a JVM, Iris must not create a second one
- [ ] `FnBackend<F>` — wrap any C++ callable as a `Backend`
- [ ] `JavaBackend::invoke()` — call a static Java method with an `IrisValue`;
      subprocess worker delegates processing to Java without owning JNI setup

---

## Far

- [ ] `WasmBackend` — mirror of `JavaBackend` for wasmtime/wasmer; bridges
      `IrisValue` into WASM linear memory so plugins running in a WASM sandbox
      receive typed messages the same way subprocess workers do
- [ ] Schema evolution — reject a worker whose `TypeDescriptor` layout has
      drifted from what the host registered; catches stale `.so` at load time
- [ ] FFM backend for Java 22+ (zero-copy `MemorySegment`)
- [ ] C++26 `std::meta` — derive `TypeDescriptor` without listing fields manually
