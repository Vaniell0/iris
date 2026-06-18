# Iris

Typed values across runtimes — without glue code.

---

## Documents

| Document | What it covers |
|----------|---------------|
| [IRIS.md](IRIS.md) | Engine: TypeRegistry, IrisValue, all backends, SDK, build options |
| [IRSH.md](IRSH.md) | Language: type system, evaluation model, IPC/pipe transport, wire protocol |
| [IRISH.md](IRISH.md) | Interpreter: three modes, terminal UX, plugin discovery, DX goals |
| [WHY.md](WHY.md) | Motivation: why Iris exists, what problem it solves |
| [ROADMAP.md](ROADMAP.md) | What is done, what is next, what is far |

---

## In one line

Register a type once with `IRIS_TYPE`. Every backend — Java, IPC, OS,
async pipeline — reads the same `TypeDescriptor`. No JNI by hand. No schema
file. No codegen.

```cpp
struct Point { int32_t x, y; };
IRIS_TYPE(Point, IRIS_FIELD(Point, x), IRIS_FIELD(Point, y))

// C++26 alternative — zero field list
struct Vec3 { float x, y, z; };
IRIS_REFLECT(Vec3)   // requires -DIRIS_STDMETA=ON (GCC 16+)
```

---

## Build

```bash
# full build
cmake -B build -GNinja \
  -DIRIS_JAVA_BACKEND=ON \
  -DIRIS_OS_BACKEND=ON  \
  -DIRIS_STDEXEC=ON     \
  -DIRIS_STDMETA=ON     # GCC 16+ only
cmake --build build

# minimal — no JVM, no OS backend
cmake -B build -DIRIS_JAVA_BACKEND=OFF -DIRIS_OS_BACKEND=OFF
```

With Nix: `nix develop` drops into a GCC 16 shell with all dependencies.
