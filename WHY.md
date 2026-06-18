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
and reject an incompatible plugin before it runs — all through the same
registry, without knowing plugin internals.

The common case: a plugin system today passes messages as `void* data,
size_t len` — an opaque blob both sides must agree on by convention.
Replace that with `IrisValue`. The host knows the type of every message,
can route it to the right backend, and can call into Java for processing,
without any JNI in the plugin or the host. The type contract lives in the
descriptor, not in a shared header that drifts.

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

---

## The constraint challenge

Iris deliberately removes one freedom: you cannot define new types at
runtime. All types are registered at startup and the registry is frozen
before the first line of irsh executes.

This looks like a limitation. It is actually a forcing function.

When you cannot invent types on the fly, you have to think about your
data model before writing a single line of script. What are the entities?
What fields do they have? What is the layout? The answer goes into a C++
struct with `IRIS_TYPE`, and from that point on irsh, Java, Rust, and
Python all share exactly that definition — no drift possible.

The payoff: a Doom-like game loop where the game engine is a Java backend
and the game logic is an irsh script becomes a real architectural question,
not a toy. Every entity — `Player`, `Enemy`, `MapSector`, `BulletEvent` —
must be declared in C++ first. The irsh script describes what happens
each tick:

```
let visible = @java("World.entities") | filter sector == player.sector
                                      | filter health > 0
visible | @java("Renderer.draw")
visible | filter dist < 64 | @java("AI.think")
```

Java renders. C++ owns the memory. irsh is the tick description — typed,
lazy, zero-allocation in the script layer. A pipeline that drops a frame
because the filter returns nothing leaves the renderer with nothing to
draw — it does not crash, it does not render stale data, it does not
open a file and wipe it. The empty-value guarantee is structural.

Tying your hands to a pre-declared type system is not a bug in the
design. It is the design. The people who find that exciting are the ones
who will build something worth using.
