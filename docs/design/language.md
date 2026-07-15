# irsh — language design

This document explains **why** irsh looks the way it does. It is not a
reference — for exact syntax, type rules, and every operator, read
[`../reference/irsh.md`](../reference/irsh.md). Read this if you want
to understand the choices before you argue with them.

For vocabulary, read [`../glossary.md`](../glossary.md). For where the
language sits inside the project, read [`architecture.md`](architecture.md).

---

## The one-line pitch

> irsh is bash's ergonomics with the type discipline of a compiled
> language and no runtime cost for either.

Everything below unpacks that. If you already know Unix pipes and any
typed language, you can skim to the tables.

---

## The central choice — everything is a function

Every `irsh` construct is a function call, and `|` is left-to-right
function composition. There are no statements, no keywords with
special evaluation rules, no eval-order surprises. Two equivalent
notations for the same pipeline:

```
ls "/var/log" | filter size > 4096 | sort by: size | print
# reads as:
print(sort(filter(ls("/var/log")), by: size))
```

The `|` form is the everyday one. The parenthesised form is what the
parser lowers to — it exists so you can reason about scope and
composition using ordinary function rules.

**Consequences of this choice:**

- Learning irsh is learning three things: what backends exist, what
  types they emit, what fields those types have. There is no separate
  "language" to learn on top.
- A new backend (`@math.avg`, `@sqlite.query`) becomes available in
  the language the moment it registers. No parser change, no keyword
  reservation, no grammar update.
- Type checking is function-argument checking. `filter size > 4096`
  requires that the upstream stream's element type has a numeric
  `size` field — a plain function-signature check.

This is the same idea as Elixir's `|>`, F#'s `|>`, or Nim's UFCS. It
is not new. What is new is combining it with a **content-addressed
type registry** so the function signatures agree across processes
and languages without a schema file.

### Why not classes / closures / loops

irsh describes *how data moves and how it transforms*. Loops over
arbitrary logic, encapsulation, inheritance — these are things you
express by calling into a real programming language (`@java`,
`@python-plugin`, `./my-binary`). Adding them to irsh would grow the
surface without solving a problem.

The one exception is a named-pipeline alias — see the `let` section
below. It gives you the reusable-function feel without adding
closures.

---

## Namespaces and the `@` sigil

Every backend is addressed by `@namespace.op`. There is one flat
namespace per registered backend, so name conflicts are visible at
registration time, not at first call.

```
@os.ls("/var/log")     # namespace: os, op: ls, config: "/var/log"
@base.filter(size > 4096)
@ipc("./worker.sock")  # backend has no ops — one call is the whole use
@java("Cls.method")    # backend accepts a method spec as config
```

**Config in `()`.** Inside a config the parser is in *string
context*: barewords are string tokens.

```
@os.ls(/var/log)              # valid — path bareword
@os.exec(git log --oneline)   # valid — command bareword
```

Outside `()` — in filter expressions, struct literals, `let` right
sides — quotes are required to tell a string apart from an
identifier:

```
filter name contains "init"     # "init" is a string literal
filter name contains init       # init is a bareword — error, no such variable
```

Path literals (`./x`, `/x`, `~/x`) are their own token class. In a
pipeline stage position they desugar to `@os.exec`:

```
./my_filter --flag       →     @os.exec("./my_filter --flag")
```

This is why `./` never needs a special irsh sigil: it is already
unambiguous in every context it can appear.

---

## `import` — the sugar, and why it exists

By default, backend ops are addressed by their fully qualified name:
`@base.filter`, `@os.ls`, `@math.sum`. That is unambiguous but
verbose. `import` brings a namespace into scope so the `@ns.` prefix
can be dropped for that scope's operations.

```
import @math                          # brings sum, avg, min, max, … (whatever @math exposes)
import @git                           # brings @git ops as bare words

# after: import @math; import @git
git.log | filter author == "vanya" | select ts | avg
```

The **current shipped syntax** is `import @ns` (whole-namespace) and
`import @ns.subns` (dotted namespace). See
`src/irish/parser/parser.cpp` (`parse_import`) and
`src/irish/session/session.hpp` for the authoritative behaviour.

**The two blessed short names — auto-imported.** `@base` and `@os`
are added to every session's import table automatically
(`Session::imports_{"os", "base"}`, applied via `make_import_table`
at every parse). This keeps `ls | filter | print` one-liners
working without ceremony. Plugins (`@math`, `@git`, `@k8s`) must
declare themselves via explicit `import`.

**Resolution order** (first match wins):

1. Explicit `@ns.op` in the call — always resolves without ambiguity.
2. Bare `op` in a stage position — resolved against the current import
   table (which is `@base` + `@os` + everything the session has
   `import`ed).
3. If two imported namespaces expose the same short name, the
   pipeline is rejected at parse time with an error naming both
   sources. Fix: use the fully qualified `@ns.op` for the ambiguous
   call.

**Scope.** `import` is a statement, evaluated in source order.
Inside a script it applies from the `import` line to end of file.
In the REPL it persists for the rest of the session and can be
undone with `:unimport @ns` (planned; see reference).

### Selective imports and aliases — planned

Two variants exist in the design but are not shipped yet. They are
marked here so a contributor sees the target state before writing
new grammar:

```
import filter, sort from @base       # selective — only these names
import @math as m                    # alias — call as m.sum, m.avg
```

Rationale: once the community namespace grows (imagine `@aws`,
`@k8s`, `@grafana` all installed), whole-namespace import will
start colliding. Selective import and aliases are the escape
hatch. Both should be additive on the parser and are tracked in
ROADMAP under "irsh language: selective import + alias".

---

## Types are for the checker, not the user

irsh has a type system, but you rarely name types. You name fields.

```
ls | filter size > 4096
```

The checker resolves `size` against `DirEntry` (the type `@os.ls`
emits), confirms it is `I64`, confirms `4096` is a compatible
literal, and only then does the pipeline begin executing. If the
field is unknown or the kind is wrong, the pipeline never runs.

**Three type categories** exist in the type system, but only one is
visible in ordinary use:

| Category            | User writes it? | Where it appears                                         |
|---------------------|-----------------|----------------------------------------------------------|
| Scalar              | rarely          | literal (`4096`, `"init"`, `true`) or final `head 1` result |
| `LazyStream<T>`     | never — implicit | every pipeline stage's output                            |
| `Result<T, E>`      | never — implicit | every stage internally (surfaces as `??` and `?|`)      |

You never write `LazyStream<DirEntry>`. You write `ls | filter …`
and the checker knows. This is the tradeoff — irsh has less
expressive typing than a general-purpose language, but the types
that exist are 100 % inferred.

**Struct types** you can declare with `type` (session-scoped) or
they come from C++ via `IRIS_TYPE` (global-scoped, registered
before freeze). Either way, the `TypeId` is the same content-address
hash — a session type declared in irsh is wire-compatible with a
C++ struct of identical layout.

Full type system spec: [`../reference/irsh.md#type-system`](../reference/irsh.md).

---

## Evaluation model — lazy by default, materialise on demand

```
let logs = ls "/var/log"                 # 0 syscalls — cursor stored
let big  = logs | filter size > 4096     # 0 syscalls — chained cursor
big | sort by: size | print              # NOW opendir/readdir + sort + print
```

`LazyStream<T>` is a pull-based cursor. Each stage in a pipeline
calls `recv()` on its upstream and processes one element at a time.
Memory usage is proportional to the largest single element, not to
the stream length.

The only exception is `sort`, which must materialise the whole
stream to order it. Documented at
[`../reference/irsh.md#standard-backends`](../reference/irsh.md).

**When you want a snapshot:**

```
let snap = ls "/var/log" | collect     # materialise into Vec<DirEntry>
snap | filter size > 4096 | print      # 0 syscalls — reads from memory
snap | sort by: mtime desc | head 5    # 0 syscalls — same
```

`collect` is the explicit hatch. It says: "this data set is small
enough to hold, and I want to reuse it".

### Empty-write safety

```
ls "/nonexistent" | write "output.txt"
# → output.txt is NOT created, NOT truncated
```

Sink stages (`write`, `@ipc`, `@os.exec`) do not act on their target
until the upstream produces the first successful value. If the
upstream fails or produces zero values, the sink is never invoked.
This is a guarantee, not a happy accident; it is documented in the
formal guarantees section of the reference.

---

## Session state — `let` is what you have, not what you write

irsh has one form of user-defined name binding: `let x = expr`.
There are no functions, no procedures, no anonymous lambdas. `let`
is enough because pipelines are values.

```
let big_logs = ls "/var/log" | filter size > 4096
big_logs | sort by: size | print          # use once
big_logs | sort by: mtime | head 3        # use again — re-runs ls
```

Two forms depending on the right-hand side:

| RHS                          | Stored as              | Cost per use                  |
|------------------------------|------------------------|-------------------------------|
| any lazy pipeline            | `LazyStream<T>` cursor | full re-execution             |
| ends in `collect`            | `Vec<T>` in session    | one materialisation, cheap use|
| scalar (from `head 1`, literal) | `IrisValue<Scalar>`  | free                          |

**Named-pipeline pattern.** Because `let` binds an entire pipeline,
you get the function-reuse feel:

```
let analyze = filter score > 0.8 | map { name, score } | sort by: score desc
raw_data | analyze | print          # analyze is the transform
other_data | analyze | write "top.jsonl"
```

The checker treats `analyze` as a typed pipeline fragment: it
verifies once that the input type has `score` and `name`, then reuses
that check every time `analyze` appears.

There is no first-class function type. `analyze` is a session name
for a fragment of AST. You cannot pass it as an argument to a
backend. You can only splice it into pipelines. This is enough — and
it keeps the type story simple.

Full session semantics: [`../reference/irsh.md#variables-and-session-state`](../reference/irsh.md).

---

## Comparison to nearby shells

irsh borrows syntax from bash and semantics from typed pipelines
(nushell, PowerShell, Elvish). The differentiator is the
content-addressed type system.

| Property                          | bash            | nushell          | PowerShell           | irsh                                     |
|-----------------------------------|-----------------|------------------|----------------------|------------------------------------------|
| Pipeline element                  | bytes (lines)   | Value (Nu type)  | .NET object          | `IrisValue` (registered `TypeDescriptor`) |
| Where the schema lives            | in the parser of every downstream tool | in Nu's built-in types | in .NET assemblies | in `TypeRegistry` — same hash in every language |
| Cross-language pipeline           | text, brittle   | Nu-only          | .NET-only            | inline C++, IPC, or fork+exec — same schema  |
| Type errors caught                | never (bash) / runtime (nu) | runtime      | runtime              | parse time — before any syscall           |
| Runtime type check overhead       | n/a             | dispatch per op  | dispatch per op      | zero — checked types compile to direct calls  |
| Empty-write safety                | no              | no               | no                   | guaranteed at language level              |
| Wire format for external tools    | text            | Nu-JSON (internal) | .NET remoting      | 12-byte header + payload, MIT SDK any language|
| Language surface size             | large (many builtins) | large    | very large           | tiny — every op is a backend              |

The point of the table is not "irsh is better" — it is "irsh makes
a different tradeoff." Bash is fine when types don't matter. Nu and
PowerShell are excellent when you stay inside their runtime. irsh is
the choice when your data has structure and needs to cross a runtime
boundary without a codegen step.

---

## What irsh is not

- **Not a general-purpose programming language.** No classes, no
  closures beyond named pipelines, no arbitrary loops. Reach for
  Java, Rust, Python, or a shell tool.
- **Not a serialisation framework.** The wire format exists to move
  pipeline values between processes, not to persist data. Iris
  frames are transient by design.
- **Not memory-safe in the C++ sense.** irsh runs on top of `libiris`,
  a native C++ library. What is safe: no undefined behaviour from
  pipeline errors, no silent data corruption from type mismatches.
- **Not a network protocol.** IPC and process transports use Unix
  sockets and pipes. No HTTP, no gRPC, no cloud story shipping. Use
  irsh to call into something that speaks the network.

For the full "not" list with reasoning, see
[`../reference/irsh.md#what-irsh-is-not`](../reference/irsh.md).

---

## Reading order after this file

- [`../reference/irsh.md`](../reference/irsh.md) — every construct,
  every operator, formal guarantees. This is the spec.
- [`../reference/irish.md`](../reference/irish.md) — the interpreter:
  REPL, script mode, pipeline-component mode, plugin discovery.
- [`../getting-started.md`](../getting-started.md) — from `git clone`
  to your first script.
- [`ir-strategy.md`](ir-strategy.md) — where the language is heading
  (IR, native compile, WASM, remote).
