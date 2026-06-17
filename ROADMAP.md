# Iris Roadmap

---

## v0.1 — Proof of concept ✓

Core type system, TypeRegistry, IrisValue with OpaqueHandle,
JVM backend with full C↔Java round-trip over JNI.
10/10 tests green.

---

## v0.2 — Inspection

**Goal:** know what will happen before it happens.

- `TypeRegistry::inspect(TypeId)` — structured description of a type
  with JNI signatures computed statically, no runtime call required
- `JavaBackend::dry_run(TypeId)` — which fields will map, which will
  be skipped, whether the class is already cached — without touching
  the JVM
- `TypeRegistry::freeze()` — lock the registry after initialization;
  any registration attempt after this point is an error; makes the
  type vocabulary immutable and auditable at runtime

---

## v0.3 — Pipeline

**Goal:** values actually flow between backends.

- Real `emit` / `recv` implementation replacing current stubs
- `pipe(c_val, backend)` — `c_to_java` → process in JVM → `java_to_c`
  as a single call with no intermediate copies
- Value queue between backends for async dispatch

---

## v0.4 — Plugin backends

**Goal:** any runtime can plug in without touching Iris source.

- `IrisBackendHandle` C ABI working end-to-end via `dlopen`
- Reference minimal backend as a standalone `.so`
- `RuntimeManager` — one JVM per process, one `JavaBackend` instance
  shared across all callers, thread-safe acquisition

---

## v1.0 — Production ready

- `iris.h` — working C ABI, not just documented
- FFM backend for Java 22+ (Project Panama, zero-copy `MemorySegment`)
- Registry schema evolution — detecting layout changes at bridge time
- C++26 `std::meta` — derive `TypeDescriptor` from struct without
  manually listing fields
