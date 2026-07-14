# Data-Oriented Design and game-loop scenarios

Design notes on how Iris's type constraints shape data layout and what
they enable — from Data-Oriented Design at the field level, up to full
game-loop scenarios where the engine is one runtime and the tick logic
is an irsh script in another.

Extracted from `WHY.md` so the motivation essay stays focused on
positioning; this file goes deeper on the mechanics.

---

## The constraint

Iris separates type registration into two registries. The **global
registry** is frozen at startup — system types (`DirEntry`, `ProcEntry`,
anything declared with `IRIS_TYPE` in C++) are immutable from the moment
the first irsh statement runs. The **session registry** is live — types
declared with `type` inside an irsh script or REPL session are added
there, never to the global one.

The constraint applies to the global registry. It is a forcing function,
not a limitation.

System types must be declared in C++ with `IRIS_TYPE` before startup.
What are the entities? What fields do they have? What is the layout?
That decision is made once, in code, and from that point irsh, Java,
Rust, and Python all share exactly that definition — no drift possible,
no accidental rename, no silent layout change.

---

## The payoff: game loop as an irsh script

Consider a Doom-like game where the game engine is a Java backend and
the game logic is an irsh script. Every entity — `Player`, `Enemy`,
`MapSector`, `BulletEvent` — must be declared in C++ first. The irsh
script describes what happens each tick:

```
let visible = @java("World.entities") | filter sector == player.sector
                                      | filter health > 0
visible | @java("Renderer.draw")
visible | filter dist < 64 | @java("AI.think")
```

Java renders. C++ owns the memory. irsh is the tick description —
typed, lazy, zero-allocation in the script layer. A pipeline that
drops a frame because the filter returns nothing leaves the renderer
with nothing to draw — it does not crash, it does not render stale
data, it does not open a file and wipe it. The empty-value guarantee
is structural.

Tying your hands to a pre-declared type system is not a bug in the
design. It is the design. The people who find that exciting are the
ones who will build something worth using.

---

## Doom as authentication

A natural extension: use Doom as an authentication challenge instead
of a CAPTCHA. The backend runs a Doom level. The irsh script monitors
player state and invalidates the session if the timer expires. Fail
the level — the script resets. Nobody has solved this problem yet
because nobody has had a typed shell with a game engine as a backend.

---

## Flat types force DOD

Current irsh types are flat: every field is a `PrimitiveKind` scalar
stored inline at a known offset. No pointers, no heap indirection,
no nested types.

This forces Data-Oriented Design by default. `Enemy` cannot contain
a `Transform` by reference — it must contain the transform fields
directly, or `EnemyId` and `TransformId` as integers with a separate
transform stream. The programmer who reaches for a pointer inside a
struct is stopped at registration time, not at a segfault three
levels deep.

This is not permanent. The wire format already supports nested
structs naturally — a nested struct is just bytes at an offset inside
the parent buffer, fully self-contained. What is missing is one field
in `FieldDesc`:

```cpp
TypeId nested_id = 0;  // 0 = scalar leaf; non-zero = inner TypeDescriptor
```

With that, `type Transform { pos: Vec2, scale: Vec2, rot: F32 }` works
in irsh, `filter transform.pos.x > 0` becomes a valid path expression,
and the wire format does not change at all.

---

## What is permanent

A field that stores a pointer (`Str`, `OpaqueHandle`) can never be
wire-safe. The pointer is valid in the sender's heap and nowhere else.
That constraint is not a design choice — it is physics. Inline bytes
at a known offset are the only wire-safe primitive, and everything in
Iris is built on that fact.

Nested structs will land as another inline layout, not as pointers.
DOD is the default, and staying wire-safe is what makes cross-runtime,
cross-machine transport free.
