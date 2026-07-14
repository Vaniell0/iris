# Glossary

Naming and terminology in Iris. Read this once — the confusion between
"Iris", "irsh", and "irish" is the single most common newcomer trip.

---

## The three names

| Name    | What it is                                                                       |
|---------|----------------------------------------------------------------------------------|
| **Iris** | The engine. C++ library (`libiris.so`) that owns the type registry, the wire format, the C ABI, and the backend concept. Also the umbrella project name. |
| **irsh** | The language. A typed scripting language whose type system reads the same `TypeDescriptor` the engine uses. Everything is a pipeline of backend calls. |
| **irish** | The binary. Interprets irsh scripts, hosts the REPL, discovers plugins, drives the type registry. This is what you install. |

Rule of thumb: **you build against Iris, you write irsh, you run irish**.

Iris does not depend on irsh. You can use `libiris` directly from
Python, Rust, or C++ without ever touching irsh syntax.

---

## Core types

### `TypeId`

A 64-bit hash of a type's name and full field layout (field names,
kinds, offsets, sizes). Two programs that define the same struct
independently agree on `TypeId` without coordination. Wire messages
carry `TypeId` as their first eight bytes — a receiver knows whether
it can decode a frame before touching the payload.

Same insight as COM GUIDs, applied to content instead of a registry.

### `TypeDescriptor`

Runtime description of a struct: name, total size, ordered list of
`FieldDesc`. The single source of truth for how a value is laid out
in memory. Every backend consults it; nobody rewrites what it says.

### `IrisValue`

A `TypeId` plus one of three payloads:

- `IrisBuffer`   — flat bytes, ref-counted, zero-copy, wire-safe.
- `OpaqueHandle` — type-erased pointer (JVM global ref, WASM extern).
                   Process-local.
- `string`       — C++ heap string. Process-local.

Every value in the system is an `IrisValue`. Move it, don't copy it —
`OpaqueHandle`'s copy constructor is deleted for a reason.

### `Backend`

Any C++ type satisfying the `Backend` concept (`can_handle`, `emit`,
`recv`, `runtime_name`). No base class, no vtable overhead, verified at
compile time. Backends are the extension point: `@os`, `@base`, `@ipc`,
`@java`, and every user-authored plugin are all backends.

### Wire-safe

A field kind is wire-safe if its bytes are self-contained inside the
struct payload. Numeric kinds (`I8..F64`, `Bool`), `CStr[N]`, and
`Bytes[N]` are wire-safe. `Str` (a heap pointer) is not. A struct is
wire-safe if all its fields are.

The parser refuses to send a non-wire-safe value through `@ipc`, `@exec`,
or `./binary`. The check happens at parse time, not at transmission.

---

## Registries

### Global registry

`TypeRegistry::global()`. Populated at engine startup via `IRIS_TYPE`
macros or `iris_type_register`. **Frozen** before the first irsh
statement runs. All system types (`DirEntry`, `ProcEntry`, backend
types) live here. Immutable during script execution.

### Session registry

`Session::session_types()`. A separate registry populated by `type`
declarations inside an irsh script or REPL session. Never frozen.
Session types get the same `TypeId` formula as global ones, so a
session type is wire-compatible with any peer declaring an identical
layout.

Session types do not survive process restart and are invisible to C++
code at compile time.

### `freeze()`

The one-way switch that promotes the global registry from mutable to
immutable. Called once in `main()` after all `IRIS_TYPE` registrations
and plugin scans complete. After freeze, all `TypeId`s are stable for
the process lifetime — backends may cache them safely.

---

## Language pieces

### Backend call

`@namespace.op(config)`. The atom of an irsh pipeline. `ls "/var/log"`
is sugar for `@os.ls("/var/log")`.

### Pipeline

Backend calls joined by `|`. Left-to-right function composition —
`ls | filter | sort | print` reads as `print(sort(filter(ls())))`.

### `let`

Binds a pipeline (lazy) or a materialised `Vec` to a session
variable. `let x = ls` stores a lazy cursor; `let x = ls | collect`
materialises.

### `type Name { ... }`

Declares a session type. Same `TypeId` formula as `IRIS_TYPE` in C++.

### `LazyStream<T>`

The static type of any pipeline before its sink. Elements pulled one
at a time; memory usage is O(1) in the stream length.

### Sink

A pipeline stage that consumes without producing (`print`, `write
"path"`, `@ipc(addr)`). A pipeline without a sink is a no-op in
script mode; the REPL auto-prints.

### Empty-write guarantee

If a sink stage (`write "path"`) never receives a value from its
source chain, the sink does not run. `write` does not create,
truncate, or touch its target file when the source produces zero
values or errors.

---

## Transport modes

| Mode     | Trigger                       | Cost                                             |
|----------|-------------------------------|--------------------------------------------------|
| Inline   | All stages in-process         | Zero-copy for `IrisBuffer`; C++ function calls   |
| IPC      | `@ipc("./sock")`              | Wire format over Unix socket; long-lived peer    |
| Process  | `./path` or `@os.exec`        | Wire format over fork+pipe(2); one-shot peer     |

The mode is chosen by syntax, not by declaration.

---

## Meta

### irish daemon

Persistent `irish` process listening on an IPC socket. Loads
backends and freezes once, serves many script invocations. Solves
per-invocation plugin reload overhead for scripts run in tight loops.
Roadmap item, not shipped yet.

### IrisIR

The intermediate representation for irsh pipelines. See
`docs/design/ir-strategy.md`. Not shipped yet.

### Retranslation

Compiling a hot pipeline stage to native code at runtime. Design
target, not a shipped feature. Requires IrisIR (see above).
