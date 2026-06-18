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

**The Iris engine** is not a shell and not a scripting language. It is
the substrate — the type registry, the wire format, the backend contract.
**irsh** is the typed scripting language built on top of it. **irish** is
the interactive shell and interpreter that runs irsh. The engine does not
depend on either — you can use the C ABI directly, from Python, Rust, or
Java, without ever touching irsh syntax.

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

Iris separates type registration into two registries. The **global
registry** is frozen at startup — system types (`DirEntry`, `ProcEntry`,
anything declared with `IRIS_TYPE` in C++) are immutable from the moment
the first irsh statement runs. The **session registry** is live — types
declared with `type` inside an irsh script or REPL session are added
there, never to the global one.

The constraint applies to the global registry. It is a forcing function,
not a limitation.

System types must be declared in C++ with `IRIS_TYPE` before startup.
What are the entities? What fields do they have? What is the layout? That
decision is made once, in code, and from that point irsh, Java, Rust, and
Python all share exactly that definition — no drift possible, no
accidental rename, no silent layout change.

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

A natural extension of this idea: use Doom as an authentication challenge
instead of a CAPTCHA. The backend runs a Doom level. The irsh script
monitors player state and invalidates the session if the timer expires.
Fail the level — the script resets. Nobody has solved this problem yet
because nobody has had a typed shell with a game engine as a backend.

---

## Flat types and DOD

Current irsh types are flat: every field is a `PrimitiveKind` scalar stored
inline at a known offset. No pointers, no heap indirection, no nested types.

This forces Data-Oriented Design by default. `Enemy` cannot contain a
`Transform` by reference — it must contain the transform fields directly,
or `EnemyId` and `TransformId` as integers with a separate transform stream.
The programmer who reaches for a pointer inside a struct is stopped at
registration time, not at a segfault three levels deep.

This is not permanent. The wire format already supports nested structs
naturally — a nested struct is just bytes at an offset inside the parent
buffer, fully self-contained. What is missing is one field in `FieldDesc`:

```cpp
TypeId nested_id = 0;  // 0 = scalar leaf; non-zero = inner TypeDescriptor
```

With that, `type Transform { pos: Vec2, scale: Vec2, rot: F32 }` works in
irsh, `filter transform.pos.x > 0` becomes a valid path expression, and
the wire format does not change at all.

What is permanent: a field that stores a pointer (`Str`, `OpaqueHandle`)
can never be wire-safe. The pointer is valid in the sender's heap and
nowhere else. That constraint is not a design choice — it is physics.
Inline bytes at a known offset are the only wire-safe primitive, and
everything in Iris is built on that fact.
