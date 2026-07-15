# Iris Ecosystem

How the project is organised, who owns what, and how a new backend
goes from idea to being available in the shell.

---

## Repository structure

```
iris-lang (GitHub organisation)
│
├── iris              — kernel + language + interpreter (this repo)
│     libiris         — TypeRegistry, IrisValue, wire format, C ABI, SDK
│     irsh            — parser, checker, executor, BackendRegistry interface
│     irish           — binary: plugin loader, REPL, script runner
│
├── iris-backend-os   — @os.ls, @os.ps, @os.env, @os.exec
├── iris-backend-base — @base.filter, @base.sort, @base.map, @base.print, …
│
│   (community-owned)
├── iris-backend-math — @math.sum, @math.avg, …
├── iris-backend-git  — @git.log, @git.status, @git.diff, …
├── iris-backend-k8s  — @k8s.pods, @k8s.events, …
│
├── iris-bridge-python — ctypes bindings for type registration + wire format recv/emit
├── iris-bridge-rust   — safe wrapper over C ABI
├── iris-bridge-go     — CGO wrapper
└── iris-bridge-java   — currently in iris/src/backend/java — will be extracted
```

**Kernel owns**: TypeId algorithm, wire format, C ABI headers (`sdk/`), freeze semantics.
These are stable. A TypeId computed today will match one computed in five years.

**Backend repos own**: their namespace (`@os.*`, `@git.*`), their TypeDescriptors,
their release cycle. They depend only on `sdk/irsh_backend.h` — the stable C ABI.
They do not depend on each other.

**Irish owns**: plugin loading FSM, REPL UX, startup sequence.
It knows about backends only through BackendRegistry — never by name.

---

## Three backend modes

Every backend operation runs in one of three modes. The mode is determined
by how the backend is registered, not by the pipeline syntax.

### Mode 1 — Inline (.so plugin)

Backend compiled to a shared library. Runs in the irish process. Zero IPC.
Values move as `IrisValue` C++ objects — no serialisation, no copy for
`IrisBuffer` payloads.

```
┌────────────────────────────────────────────┐
│ irish process                              │
│  @os.ls → @base.filter → @base.sort → … │
│           IrisGen pull chain in memory    │
└────────────────────────────────────────────┘
```

**Who writes it**: C, C++, Rust (via `extern "C"`), any language with a C FFI.
**How**: implement `IrshBackendHandle` vtable, export `iris_irsh_backend_create`.
**Where**: `.so` file in `~/.iris/plugins/` or project directory.
**Startup**: scanned and loaded during irish startup FSM, before REPL opens.

### Mode 2 — IPC (persistent process via @ipc)

Backend is a long-running process that connected to irish before the pipeline
runs. Communication over Unix domain socket. Values are serialised to wire
format (12-byte header + payload).

```
┌──────────┐  wire frames  ┌────────────────┐
│  irish   │ ────────────► │ backend daemon │
│          │ ◄──────────── │ (any language) │
└──────────┘               └────────────────┘
```

**Who writes it**: any language — Python, Rust, Go, Java.
**How**: register types via `sdk/iris_registry.h` (or language bridge), listen on socket,
read wire frames, process, write wire frames back.
**Where**: started separately; path given as `@ipc("./my.sock")` or `--ipc` flag.
**Startup**: connection established at pipeline type-check time, not at first value.
Socket unavailable → type-check error, not runtime error.

### Mode 3 — Process (fork+exec via @os.exec or ./path)

Backend is a one-shot binary. Irish forks it per pipeline run, connects
stdin/stdout via `pipe(2)`. Same wire format as Mode 2.

```
┌──────────┐ fork+pipe  ┌─────────────┐
│  irish   │ ─────────► │ ./my_filter │
│          │ ◄───────── │             │
└──────────┘            └─────────────┘
```

**Who writes it**: any language with 50 lines of code.
**How**:
1. Register the types you consume and produce (one call per type)
2. Read wire frames from `stdin`
3. Write wire frames to `stdout`
4. Exit 0 on clean EOF

**DX minimum** (from [docs/reference/irish.md](docs/reference/irish.md)):
> Writing an Iris-aware utility should require nothing beyond: register types,
> read stdin, write stdout, exit 0.

Wire format: 12 bytes header (type_id u64 LE + size u32 LE) then raw struct bytes.
See [docs/reference/wire-format.md](docs/reference/wire-format.md). Python example: 50 lines. Rust example: 30 lines.

---

## Development workflow — writing a new backend

### Inline backend (`@math.*`)

```
1. Create repo: iris-backend-math
2. Add dependency: sdk/irsh_backend.h (copy from iris, pin to a version)
3. Define your types:
     struct Sum { int64_t value; };
     IRIS_TYPE(Sum, IRIS_FIELD(Sum, value))
4. Implement IrshBackend subclass:
     class MathBackend : public iris::irsh::IrshBackend {
         IrType check(op, config, input, ...) override { ... }
         IrisGen make_gen(op, config, desc, upstream) override { ... }
     };
5. Export:
     IRIS_IRSH_BACKEND(MathBackend)   // generates iris_irsh_backend_create
6. Build: libmath.iris.so
7. Test: copy to ~/.iris/plugins/, run irish, type @math.sum <TAB>
8. Ship: GitHub release with the .so artifact
```

### Process backend (any language)

```
1. Register types — use sdk/iris_registry.h or a language bridge
2. Read frames loop:
     while (frame = read_frame(stdin)):
         result = process(frame)
         write_frame(stdout, result)
3. Compile / package
4. Use: ls | ./my_filter | print
         @os.exec("./my_filter") | @base.print
```

### IPC backend (persistent service)

```
1. Same type registration as process backend
2. Listen on Unix socket before connecting to irish:
     ipc = IpcBackend::listen("./myservice.sock")
3. Run as daemon
4. User connects with @ipc("./myservice.sock") in pipeline
5. Connection is verified at type-check time
```

---

## Compatibility and updates

### What is stable

| Component | Stability guarantee |
|-----------|---------------------|
| TypeId algorithm (FNV-64 over name+layout) | Permanent. Changing it breaks all wire-format compatibility |
| Wire frame layout (8+4 header, raw payload) | Permanent |
| `sdk/iris_registry.h` C ABI | Semver. New slots are additive (api_size guard) |
| `sdk/irsh_backend.h` C ABI | Semver. New slots additive |

### What can change

| Component | When it changes |
|-----------|----------------|
| IrType variants | Minor: additive. Any minor release |
| BackendConfig variant | Minor: additive |
| @base.* operations | Any: new ops additive; removing an op is a major release |
| @os.* operations | Same as @base |
| irish REPL UX | Any: completion, hints, display are not stable API |

### Updating a backend

A backend compiled against `sdk/irsh_backend.h v1.2` continues to work
with irish v1.5 if the `api_size` guard allows. Irish reads `handle->api_size`
before calling any slot and skips slots it does not know about.

A backend never needs to be recompiled because TypeIds changed — TypeId is
derived from content, not from a version number. Two processes defining the
same struct independently will agree on TypeId automatically.

A backend MUST be recompiled only if it changes its own TypeDescriptors
(field names, kinds, offsets, sizes). This is a breaking change for IPC
peers that have already agreed on the old TypeId.

---

## Responsibility matrix

| What breaks | Who fixes it |
|-------------|-------------|
| TypeId collision between two backends | iris kernel — by_name_ conflict detection at register |
| Backend reports wrong IrType from check() | The backend repo |
| IPC peer sends unknown TypeId | iris kernel — rejects frame before touching payload |
| Wire-unsafe type sent via @ipc | irsh checker — caught at parse time, not runtime |
| Plugin fails VERIFIED in FSM | Plugin is skipped, warning logged, irish continues |
| irish crashes loading a .so | Plugin loader catches dlopen errors, continues |
| @math.sum returns wrong value | @math backend repo — kernel has no visibility into logic |

---

## What does NOT belong in the iris kernel repo

- Backend business logic (`@git`, `@docker`, `@k8s`)
- Language bridges beyond the C ABI (`bridges-python`, `bridges-rust`)
- Application-specific type definitions
- Plugin manifests or signatures (may be added to irish, not kernel)

The kernel's job is: move typed values, check type safety, expose stable C ABI.
Everything else is a backend or a bridge in its own repo.
