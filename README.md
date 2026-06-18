# Iris

Typed values across runtimes — without glue code.

Describe a type once. Every backend — Java, IPC, OS, async pipeline — reads
the same `TypeDescriptor`. No JNI by hand. No schema file. No codegen.

---

## Registration

```cpp
// Explicit fields — works on any C++23 compiler
struct Point { int32_t x, y; };
IRIS_TYPE(Point, IRIS_FIELD(Point, x), IRIS_FIELD(Point, y))

// Zero fields — C++26 reflection derives everything automatically
struct Vec3 { float x, y, z; };
IRIS_REFLECT(Vec3)   // requires -DIRIS_STDMETA=ON (GCC 16+)
```

The same `TypeId` for the same layout, across processes and builds — derived
from name + field layout by FNV-64. Two binaries that define the same struct
independently will agree on its identity without coordination.

---

## Backends

| Backend | What it does |
|---------|-------------|
| `JavaBackend` | Bridges `IrisValue` into a JVM field-by-field over JNI; one `JavaVM*` per process via `RuntimeManager` |
| `IpcBackend` | Carries `IrisValue` over a Unix socketpair or named socket; zero-copy emit via `writev` scatter-gather |
| `OsBackend` | `ls`, `ps`, `env` return typed `IrisValue` streams — `DirEntry`, `ProcessInfo`, `EnvVar` |
| `FnBackend<F>` | Wraps any C++ callable as a backend; composable with `operator\|` |
| `FnBackend` composition | `fn_a \| fn_b` chains backends; the pipe operator is the pipeline |

---

## P2300 — async pipelines

```cpp
#include <execution.hpp>   // -DIRIS_STDEXEC=ON

auto result = iris::sync_wait(
    iris::just(iris::wrap(Point{3, 4}))
    | iris::via(backend)
    | iris::then([](iris::IrisValue v) { return v; })
);
```

`iris::schedule_on(thread_pool)` offloads the rest of the chain to a thread pool.
Works with any backend that satisfies the `Backend` concept.

---

## Build

```bash
# Full build (Java + OS + P2300 + P2996)
cmake -B build -GNinja \
  -DIRIS_JAVA_BACKEND=ON \
  -DIRIS_OS_BACKEND=ON  \
  -DIRIS_STDEXEC=ON     \
  -DIRIS_STDMETA=ON     # GCC 16+ only
cmake --build build

# Minimal — no JVM, no reflection
cmake -B build -DIRIS_JAVA_BACKEND=OFF -DIRIS_STDEXEC=OFF
```

With Nix: `nix develop` drops into a GCC 16 shell with all dependencies.

---

## What comes next

**irsh** — a typed shell where commands emit `IrisValue` instead of text.
Tab completion driven by `TypeDescriptor` field names. Pipelines built on
`FnBackend` composition and P2300 senders. Java ecosystem accessible
without installing anything.

---

See [WHY.md](WHY.md) for the full motivation.
See [ROADMAP.md](ROADMAP.md) for open tasks.
