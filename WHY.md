# Why Iris

---

Every runtime has its own representation of the same data.

```
C++:  struct FileEntry { int64_t size; int32_t mode; };
Java: class FileEntry  { long    size; int     mode; }
```

Moving a value between them today means choosing between: hand-written
JNI, a schema file you maintain separately from the code, or a
serialization library that copies everything twice and requires a
network framing.

None of these scale. All of them duplicate the type definition you
already wrote.

---

## The idea

A type registration is not bureaucracy. It is the moment you teach
the system how to handle that data everywhere.

Register `FileEntry` once. Iris knows the layout in C memory, the JNI
signatures for each field, and how to round-trip values between any
two runtimes without additional instructions. The type descriptor is
the knowledge — backends are just readers of it.

This means you can ask questions about a bridge before running it:
which fields will map, which will be skipped, whether the Java class
is layout-compatible with your C struct. Static inspection without
executing a single JNI call. The answer comes from the descriptor alone.

---

## Scenarios

**Typed shell.** Commands emit `IrisValue` instead of text. The shell
knows the type of every value in the pipeline — `ls | where size > 1mb`
is a numeric comparison on `i64`, not a grep. Tab completion is driven
by field names from the `TypeDescriptor`. Type errors are caught before
any process runs.

**JNI by hand is over.** You define your struct, call `IRIS_TYPE`, and
pass the value to `c_to_java`. Iris resolves field offsets, looks up
`jfieldID` per kind, and does the copies. The type you already wrote
is the only source of truth.

**Plugin substrate.** A host process registers the types it understands.
Each plugin loaded via `dlopen` registers its own types at init time.
The host can inspect any plugin's types, bridge values between plugins,
and reject unknown types — all through the same registry, without
knowing plugin internals.

**In-process IPC bridge.** A C++ daemon and a Java service run in the
same process. They share typed values through `Channel` — no socket,
no serialization, no framing. The `Channel` is a typed queue; the
`TypeDescriptor` ensures both sides agree on the layout.

---

## What Iris is not

Not a replacement for protobuf or Cap'n Proto. Those solve serialization
across a network. Iris solves in-process FFI across runtimes. The problem
is different: no wire format, no framing, no schema registry service —
just two runtimes in the same process that need to share a typed value.

Not a code generator. You do not run a tool, you do not check in
generated files. The descriptor is built at startup and consulted at
runtime.

Not a shell. Not a scripting language. Iris is the substrate — what
you build shells, pipelines, and language bridges on top of.

---

## Content-addressed identity

Every type has a `TypeId` derived from its name and field layout by
a deterministic hash. Two processes that define the same struct
independently will agree on its `TypeId` without any coordination.
This is the same insight as COM GUIDs, applied correctly: identity
derived from content, not from a registry or a counter.

---

## For whom

- Authors of language runtimes who need to pass typed values to
  other runtimes without serialization overhead
- Authors of shells and pipelines that want typed tab completion and
  inline type checking without writing a type checker
- Transport libraries that want a neutral type vocabulary shared
  across C++ and JVM without choosing sides
- Anyone who has written JNI by hand and does not want to again
