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
- [ ] `pipe(c_val)` — `c_to_java` → Java method → `java_to_c` as one call
- [ ] `OsBackend` satisfying the `Backend` concept (streaming `recv`)

---

## Cross-platform

- [ ] OS utilities as a separate `libirisos` — optional link, no GPL propagation concern
- [ ] Abstract OS layer: `#ifdef` / CMake platform guards so `ls`/`ps` compile on macOS and Windows
- [ ] `ProcEntry` fallback on non-Linux (macOS `sysctl`, Windows `EnumProcesses`)
- [ ] CI matrix: Linux, macOS, (Windows MinGW)

## Next

- [ ] `RuntimeManager` — one JVM per process, thread-safe acquisition
- [ ] `FnBackend<F>` — wrap any C++ callable as a `Backend`
- [ ] `JavaBackend::invoke()` — call a static Java method with an `IrisValue`
- [ ] `IrisBackendHandle` C ABI working via `dlopen`
- [ ] Reference minimal backend as a standalone `.so`

---

## Far

- [ ] `iris.h` — working C ABI, not just documented
- [ ] FFM backend for Java 22+ (zero-copy `MemorySegment`)
- [ ] Schema evolution — detect incompatible type layout changes at bridge time
- [ ] C++26 `std::meta` — derive `TypeDescriptor` without listing fields manually
