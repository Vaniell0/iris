# Iris — architecture

Whole-project view. Where every piece lives, what it owns, and why the
boundaries fall where they do. If you plan to write a backend, extend
the language, or embed the engine in another product — start here.

For the vocabulary (Iris / irsh / irish, `TypeId`, `TypeDescriptor`,
wire-safe) read [`../glossary.md`](../glossary.md) first.

---

## The three-layer model

Iris is deliberately split into three layers with a single dependency
direction. Nothing higher up may be imported by anything lower down.

```
┌────────────────────────────────────────────────────────────────┐
│  irish  — interpreter binary                                   │  Layer 3
│  · REPL, script runner, pipeline-component mode                │  depends on: 1, 2
│  · plugin discovery (~/.iris/plugins/*.so)                     │
│  · IDE-adjacent UX (completion, hints, error rendering)        │
└────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────────┐
│  irsh   — the language                                         │  Layer 2
│  · lexer, parser, checker, executor                            │  depends on: 1
│  · BackendRegistry — dynamic dispatch to IrshBackend vtable    │
│  · session state (let / type)                                  │
└────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────────┐
│  Iris   — the engine (libiris.so)                              │  Layer 1
│  · TypeRegistry + freeze()                                     │  depends on: nothing
│  · IrisValue, IrisBuffer, OpaqueHandle                         │
│  · wire format (12-byte header + payload)                      │
│  · Backend concept, FnBackend, Channel                         │
│  · C ABI (sdk/*.h) — MIT surface                               │
└────────────────────────────────────────────────────────────────┘
```

You can consume the engine alone from Python, Rust, Go, or C++ without
ever touching irsh — that is the point of the MIT SDK. You can also
compile irsh into `irish` and ignore embedding — that is the CLI
distribution. Neither layer is optional to the other for its own
purpose, but each is useful without the layers above it.

---

## Repository layout

Everything in this repo belongs to one of the three layers plus a
small support surface (docs, tests, examples).

```
iris/
├── include/                    Layer 1 · public C++ headers, GPL-2.0
│   ├── backend.hpp             concept Backend {…}
│   ├── channel.hpp             thread-safe IrisValue queue
│   ├── execution.hpp           P2300 sender adaptors (opt-in)
│   ├── registry.hpp            TypeRegistry, TypeDescriptor, FieldDesc
│   ├── value.hpp               IrisValue, IrisBuffer, OpaqueHandle
│   └── backend/                per-backend public headers
│
├── src/                        Layer 1 · engine implementation, GPL-2.0
│   ├── registry.cpp            TypeId computation + registry state
│   ├── iris_backend_handle.cpp C ABI thunk into C++ Backend concept
│   └── backend/
│       ├── ipc/                Unix-socket transport
│       ├── java/               JNI bridge, JavaVM management
│       └── os/                 ls/ps/env, OsStream<> CRTP (Apache-2.0)
│
├── src/irish/                  Layer 2 + 3 · language + interpreter
│   ├── lexer/                  tokens, shorthand expansion, path literals
│   ├── parser/                 AST construction (typed after checker)
│   ├── checker/                type resolution against TypeRegistry
│   ├── exec/                   Executor::run, lazy generator chain
│   ├── session/                let bindings, session TypeRegistry
│   ├── backend/                built-in irsh backends (BaseIrshBackend,
│   │                           OsIrshBackend) — compiled in, not plugins
│   └── main.cpp                mode dispatch: REPL / script / pipeline
│
├── sdk/                        Layer 1 · public C ABI, MIT
│   ├── iris_registry.h         iris_type_register + friends
│   ├── iris_backend.h          IrisBackendHandle vtable
│   ├── irsh_backend.h          IrshBackendHandle vtable (for language plugins)
│   ├── LICENSE                 MIT — commercial-embedding clean
│   ├── cpp/                    header-only C++ umbrella + macros
│   └── py/                     ctypes bindings
│
├── tests/                      GTest, one file per subsystem
├── examples/                   demo / jni_bridge / os_pipeline / worker
├── docs/                       this tree
├── CMakeLists.txt              build options — see "CMake matrix" below
├── flake.nix                   Nix dev shell (GCC 16, replxx, GTest)
└── README.md WHY.md ROADMAP.md CONTRIBUTING.md ECOSYSTEM.md LICENSE
```

Rule of thumb for "where does my change go":

- Touches `TypeId`, `IrisValue`, wire header, `Backend` concept → `include/` + `src/` (Layer 1). This is the compatibility zone; changes need two approvals.
- New OS command (`@os.foo`) → `src/backend/os/` under Apache-2.0.
- New irsh built-in (`@base.foo`, new operator) → `src/irish/backend/` + parser/checker plumbing.
- New language *external* backend (`@ffmpeg.*`) → **its own repo**. See `../../ECOSYSTEM.md`.
- Anything the world outside compiles against → `sdk/`. This is the MIT surface. New slots are additive, guarded by `api_size`.

---

## Data flow — how a value moves

The engine has one type — `IrisValue` — and one transport contract —
`Backend::emit`/`recv`. Everything else is composition.

### Inline pipeline (Mode 1)

```
              (backend concept — compile-time, zero vtable)

  @os.ls("/var/log")  ──►  @base.filter  ──►  @base.sort  ──►  @base.print
       │                        │                  │                │
       │  OsStream<LsStream>    │  Fn(IrisValue)   │  materialise   │
       │  opendir/readdir       │  → IrisValue     │  Vec<IrisValue>│
       │  lazy pull, one dirent │  short-circuit   │  quicksort     │
       │  per recv()            │  on empty        │  by field ptr  │
       ▼                        ▼                  ▼                ▼
  ┌───────────────────────────────────────────────────────────────────┐
  │  IrisValue<DirEntry> — IrisBuffer(280 bytes), ref-counted        │
  │                                                                   │
  │  No copies. Every stage receives the same IrisBuffer via         │
  │  shared_ptr<byte[]>. Filter reads .size at offset 0 directly.    │
  └───────────────────────────────────────────────────────────────────┘
```

Everything lives in one process, one thread pool. `Executor::run`
walks the checked AST left-to-right and returns a
`std::function<std::optional<IrisValue>()>` — the sink pulls that
generator to completion.

### IPC pipeline (Mode 2)

```
  irish process                        worker daemon (any language)
  ┌──────────────────────┐             ┌──────────────────────────┐
  │  ps | filter | @ipc  │             │  read frames, register    │
  │                      │             │  types, run its own       │
  │  IpcBackend::emit    │             │  pipeline                 │
  │      │               │             │      ▲                    │
  │      ▼ writev()      │             │      │ recv_one()         │
  │  ┌─────────┐         │             │      │                    │
  │  │ header  │ payload │  Unix       │  ┌───┴─────┐              │
  │  │ 12 B    │ N bytes │  socket     │  │ header  │ payload      │
  │  └─────────┘         │  ──────────►│  │  12 B   │  N bytes     │
  │                      │             │  └─────────┘              │
  └──────────────────────┘             └──────────────────────────┘
```

One `writev(2)` per frame — header and payload in a single syscall.
Connection established at parse time, so socket failure surfaces as a
parse error, never mid-pipeline. Wire-safety (`Str` prohibited)
checked at parse time too.

### Process pipeline (Mode 3)

```
  irish process                    ./my_filter (any language, 50 lines)
  ┌──────────────────┐             ┌─────────────────────────────┐
  │  ls | ./filter   │             │  register type,             │
  │                  │             │  read wire frames from       │
  │  fork()          │             │  stdin, write to stdout,     │
  │  pipe(2), pipe(2)│             │  exit 0 on EOF               │
  │  execvp("./f")   │             │                              │
  │  serialise ▼     │             │  ▲ deserialise               │
  │  ┌─────────────┐ │  pipe(2)    │  │                           │
  │  │ frame frame │─┼────────────►│  ├─ process ─┐               │
  │  │ frame frame │◄┼─────────────│──┘           ▼               │
  │  └─────────────┘ │  pipe(2)    │  ┌───────────────────────┐   │
  └──────────────────┘             │  │ output frames stdout  │   │
                                   │  └───────────────────────┘   │
                                   └─────────────────────────────┘
```

Same wire format as Mode 2, transport is `pipe(2)` from fork+exec.
No shell involved — `execvp`, not `popen`. Argument injection is
impossible by construction: irsh serialises typed values, not shell
words.

Full protocol spec: [`../reference/wire-format.md`](../reference/wire-format.md).

---

## The extension model — backends

Every operation in irsh is a backend call. The four built-in
namespaces (`@os`, `@base`, `@ipc`, `@java`) are backends; every
future feature is a backend. This is not aspiration — it is how the
parser works. Adding `@math.sum` requires zero parser or checker
changes.

Three ways to author a backend, chosen by what you have and what you
need. All three go through the same `IrshBackendHandle` C ABI.

| Mode      | Who writes it                                    | Runs where       | Startup cost         | Best for                                 |
|-----------|--------------------------------------------------|------------------|----------------------|------------------------------------------|
| Inline    | C/C++/Rust (extern "C"), any C-FFI language      | irish process    | dlopen, ~ms          | tight loops, hot pipelines, small deps   |
| IPC       | any language                                     | separate daemon  | socket connect       | long-running services, JVM/Python heavy  |
| Process   | any language, ~50 lines                          | fork per invoke  | fork+execvp          | one-shot filters, cross-language experiments |

The **only** promise a backend needs to make is: implement the vtable
in `sdk/irsh_backend.h`, register types via `sdk/iris_registry.h`,
and read/write wire frames if you cross a process boundary. Nothing
else. There is no manifest file, no signing, no registration server.

Details on writing a backend, wire-format receiver examples in
Python and Rust, and the compatibility matrix live in
[`../../ECOSYSTEM.md`](../../ECOSYSTEM.md) and
[`../reference/wire-format.md`](../reference/wire-format.md).

---

## Plugin discovery — startup FSM

`irish` runs a fixed sequence at startup. The order matters because
the type registry must be frozen before parsing the first user
statement.

```
  START
    │
    ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ 1. Register built-in types (DirEntry, ProcEntry, EnvEntry,  │
  │    TextLine, …) via IRIS_TYPE in engine + irsh backends.    │
  └─────────────────────────────────────────────────────────────┘
    │
    ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ 2. Scan ~/.iris/plugins/*.so and $PWD/*.iris.so.            │
  │    For each: dlopen → iris_backend_create → register.       │
  │    Failures are logged and skipped. Never crash on plugin.  │
  └─────────────────────────────────────────────────────────────┘
    │
    ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ 3. If --classpath given: JavaBackend::register_class()      │
  │    for every public class in the jar. Adds Java types to    │
  │    the global registry.                                     │
  └─────────────────────────────────────────────────────────────┘
    │
    ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ 4. TypeRegistry::global().freeze()                          │
  │    Atomic switch to immutable. No more registrations from   │
  │    this point on. Session types (via `type` in scripts)     │
  │    live in a separate SessionRegistry that stays mutable.   │
  └─────────────────────────────────────────────────────────────┘
    │
    ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ 5. Mode dispatch:                                           │
  │    · isatty(stdin)  → REPL                                  │
  │    · argv has file  → Script runner                         │
  │    · pipe on stdin  → Pipeline-component (reads $stdin      │
  │                       as wire-format stream)                │
  └─────────────────────────────────────────────────────────────┘
```

After freeze, the checker can cache `TypeDescriptor*` pointers into
its AST nodes — they will not be invalidated for the process
lifetime. This is why freeze is a one-way switch: partial mutability
would force every consumer to re-look-up.

---

## Freeze semantics — why the registry locks

`TypeRegistry::freeze()` is not a performance optimisation on its
own. It is a safety mechanism that becomes valuable when combined
with two other invariants:

1. **Content-addressed identity.** `TypeId` is derived from the
   struct itself. Two processes agree without coordination.
2. **Wire-safety-at-parse-time.** The checker refuses to send a
   value with a `Str` field across a process boundary. This decision
   depends on the `TypeDescriptor` being stable.

If the registry could mutate mid-execution, both invariants would
have holes: a plugin loaded after parse could register a new
`FieldDesc` with a `Str` at an old offset, silently invalidating a
pipeline the checker had already approved.

The freeze contract is documented in
[`../contracts/type-registry-freeze.md`](../contracts/type-registry-freeze.md).

Once the daemon model lands (see below), freeze also becomes a
performance win: many script invocations share one register-and-freeze
pass instead of paying it per invocation.

---

## The daemon model (planned, not shipped)

Every irsh script invocation currently pays the full startup cost —
dlopen every plugin, register every type, freeze. For scripts run in
tight loops (`irish -e '...'` in a shell wrapper, `#!/usr/bin/env
irish` in cron jobs) this is wasteful.

The plan: `irish-daemon` starts once, does the FSM above, listens
on a Unix socket. Each new invocation of `irish` connects, ships
its `.irsh` source (or, later, its `.iir`), and receives a stream
of typed output frames back. The daemon holds the frozen registry
and the loaded plugins.

The transport layer for this already exists — `IpcBackend` is what
the daemon and client would use. What is missing is the FSM
alternate path and the CLI flag.

Roadmap item in `ROADMAP.md` under "Now — irish interpreter".

---

## CMake matrix — what actually ships

`libiris.so` and `libirisos.so` are the two artefacts. The `irish`
binary is optional and off by default (`-DIRIS_IRISH=ON`). Nothing
here is guessed — this is from `CMakeLists.txt`.

| Flag                  | Default | Effect                                                              |
|-----------------------|---------|---------------------------------------------------------------------|
| `IRIS_STATIC_RUNTIME` | OFF     | Statically link libstdc++/libgcc into binaries                      |
| `IRIS_BUILD_TESTS`    | ON      | Build GTest targets under `tests/`                                  |
| `IRIS_BUILD_EXAMPLES` | OFF     | Build `examples/` (demo, jni_bridge, os_pipeline, worker)           |
| `IRIS_JAVA_BACKEND`   | ON      | Compile JavaBackend + JNI into `libiris.so` (needs JNI headers)     |
| `IRIS_OS_BACKEND`     | ON      | Build separate `libirisos.so` (ls/ps/env). Core `libiris` OS-free   |
| `IRIS_STDEXEC`        | OFF     | Enable P2300 sender adaptors via `nvidia/stdexec`                   |
| `IRIS_STDMETA`        | OFF     | Enable P2996 auto-reflection (`IRIS_REFLECT(T)`). GCC 16+, C++26    |
| `IRIS_IRISH`          | OFF     | Build the `irish` interpreter binary from `src/irish/`              |
| `IRIS_FUZZ`           | OFF     | Build libFuzzer target for lexer+parser (requires clang)            |

Two representative configurations:

**Everything, for a full dev environment**
```bash
cmake -B build -GNinja \
  -DIRIS_IRISH=ON \
  -DIRIS_JAVA_BACKEND=ON \
  -DIRIS_OS_BACKEND=ON \
  -DIRIS_STDEXEC=ON \
  -DIRIS_STDMETA=ON \
  -DIRIS_BUILD_EXAMPLES=ON
```

**Minimal engine, for embedding in another C++ product**
```bash
cmake -B build -DIRIS_JAVA_BACKEND=OFF -DIRIS_OS_BACKEND=OFF
```

The minimal build produces `libiris.so` with no JVM, no OS
dependency, no reflection — the smallest surface a host can embed.

---

## Licence layout — why it splits this way

Iris is a mixed-licence project on purpose. The split lets one
codebase serve three different embedding stories.

| Zone                                  | Licence     | Story                                                                                            |
|---------------------------------------|-------------|--------------------------------------------------------------------------------------------------|
| Core: `src/` (excl. `os/`), `include/`| GPL-2.0     | Changes to the engine stay open. This is the growth engine of the shared substrate.              |
| OS layer: `src/backend/os/`, `include/backend/os.hpp` | Apache-2.0 | Downstream users can link `libirisos.so` in commercial code without copyleft propagation.        |
| SDK: `sdk/*`                          | MIT         | Any language runtime, any commercial code, links against the C ABI cleanly. This is the API.     |

The build-dependency exception in `LICENSE` allows the GPL core to
link against Apache-2.0 dependencies (specifically `stdexec`) when
enabled via CMake, without triggering copyleft on the dependency.

Practical rule: **if in doubt, put your code in a downstream repo
that consumes the SDK**. The kernel's job is to move typed values.
Backend business logic (`@ffmpeg`, `@git`, `@k8s`), language bridges
(`iris-bridge-python`, `iris-bridge-rust`), application types — none
of these belong in this repo.

Full ownership and repository breakdown:
[`../../ECOSYSTEM.md`](../../ECOSYSTEM.md).

---

## Reading order for a new contributor

1. [`../glossary.md`](../glossary.md) — Iris vs irsh vs irish, and the vocabulary.
2. This file — the layers, the layout, and where things live.
3. [`language.md`](language.md) — the language model (why pipelines, why no closures, `import` as sugar).
4. [`../reference/wire-format.md`](../reference/wire-format.md) — the one binary contract every peer speaks.
5. [`ir-strategy.md`](ir-strategy.md) — where portable execution is heading (IR, LLVM, WASM, remote).
6. [`../contracts/*.md`](../contracts/) — the covenants with plugin authors. Break one → major version.
7. [`../../CONTRIBUTING.md`](../../CONTRIBUTING.md) — style, review, licence rules for the actual PR.

If you finish those and still have room, the reference documents
(`../reference/iris.md`, `irsh.md`, `irish.md`) go into every knob
by name.
