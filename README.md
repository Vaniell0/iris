# Iris

Typed values across runtimes — without glue code.

Describe a type once. Iris bridges it to Java, reads it back to C,
inspects it without touching the JVM, and does all of this from a
single TypeDescriptor that any backend can read.

---

```cpp
struct Point { int32_t x, y; };
IRIS_TYPE(Point, IRIS_FIELD(Point, x), IRIS_FIELD(Point, y))

// C → Java
auto result = backend.c_to_java(iris::wrap(Point{3, 4}));

// Java → C
auto back = backend.java_to_c(*result);
assert(iris::unwrap<Point>(*back).x == 3);
```

No JNI by hand. No schema file. No codegen step.

---

The same `TypeDescriptor` powers every direction: C to Java, Java to C,
static inspection before any runtime call, and future backends (Node, Python,
any `.so` plugin) without changing the type definition.

Type identity is content-addressed — the same struct layout always produces
the same `TypeId` across processes and builds. Two binaries that define the
same type independently will agree on its identity without coordination.

---

See [ROADMAP.md](ROADMAP.md) for what comes next.
See [WHY.md](WHY.md) for why this exists.
