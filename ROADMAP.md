# Iris — Roadmap

Tasks are closed as they ship. Organised by layer:
**Iris** (engine) · **irsh** (language) · **irish** (interpreter) · **Ecosystem**

A version tag is cut when a meaningful set of tasks lands together.

---

## MVP Architecture Decisions

These decisions are intentional, not tech debt — made during the irsh MVP phase:

- **`BaseIrshBackend` and `OsIrshBackend` are built-in** (compiled into `irish`), not `.so` plugins.
  OS commands are shell primitives, like bash builtins. Zero startup cost, no dlopen overhead.
  Third-party backends (`@ffmpeg`, `@git`, `@k8s`) go as `.so` plugins.

- **`.so` plugins solve extensibility, not startup performance.** For script mode
  (`#!/usr/bin/env irish`) invoked repeatedly, the right model is `irish-daemon` over IPC
  (transport layer already exists in `src/backend/ipc/`). Daemon loads once, freezes once,
  serves all script invocations via Unix socket. This is the next phase.

- **`TypeRegistry::freeze()` is a safety mechanism, not an optimisation** in the current
  single-process model. Its optimisation value realises in the daemon model.

- **`OsStream<>` CRTP contract**: call `recv()` — never `open()` directly. `recv()` manages
  the `opened_` flag; the destructor calls `close()` only when `opened_` is true.
  Calling `open()` directly bypasses RAII and leaks the OS handle.

- **Session variables**: `let x = ls | collect` → materialises into `MatVec`
  (shared_ptr<vector<IrisValue>>). `let x = ls` → stores lazy `TypedPipeline`, re-runs on use.

---

## Done

### Iris engine

- [x] Core type registry with content-addressed TypeId (FNV-64)
- [x] IrisValue with OpaqueHandle — JNI-free core
- [x] `IrisBuffer` — zero-copy ref-counted buffer; replaces `vector<byte>`
- [x] `TypeRegistry::inspect()` — static type info without any runtime call
- [x] `TypeRegistry::freeze()` — lock registry post-init, zero-trust
- [x] `TypeRegistry::freeze()` called before first irsh statement — registry immutable during parsing
- [x] `FnBackend<F>` — wrap any C++ callable as a `Backend`
- [x] `FnBackend` composition — `fn_a | fn_b` chains via `operator|`
- [x] `Channel` — thread-safe `IrisValue` queue connecting two backends
- [x] `emit` / `recv` wired to `Channel`
- [x] `IpcBackend` — carries `IrisValue` over Unix socketpair / named socket
- [x] `IpcBackend::emit` — `writev` scatter-gather (header + payload, one syscall)
- [x] Frame size limit in `IpcBackend::recv()` — reject frames > 64 MiB
- [x] `IrisBackendHandle` C ABI — vtable of C function pointers
- [x] `sdk/iris_backend.h` (MIT) — C ABI interface decoupled from GPL core
- [x] `sdk/iris_registry.h` (MIT) — C ABI for type registration; usable from Python/Rust/Go FFI
- [x] `sdk/cpp/iris.hpp` — C++ umbrella (types + macros + wrap/unwrap); IRIS_TYPE uses C ABI
- [x] `sdk/cpp/backend.hpp` — RAII C++ wrapper over C ABI, no GPL headers
- [x] `sdk/py/iris.py` — ctypes bindings for type registration
- [x] `JavaBackend`: `c_to_java` / `java_to_c` round-trip over JNI
- [x] `JavaBackend::dry_run()` — bridge analysis without touching the JVM
- [x] `JavaBackend::invoke()` — call a static Java method with an `IrisValue`
- [x] `RuntimeManager` — one `JavaVM*` per process, thread-safe acquisition
- [x] `pipe(c_val)` — `c_to_java` → Java method → `java_to_c` as one call
- [x] `OsBackend` satisfying the `Backend` concept — lazy streaming via CRTP `OsStream<>`
- [x] OS utilities: `ls`, `ps`, `env` → typed `IrisValue` streams (`DirEntry`, `ProcEntry`, `EnvEntry`)
- [x] OS commands return `expected<vector<IrisValue>, OsError>` on failure
- [x] `libirisos` as a separate CMake target — core `libiris` has no OS dependency
- [x] Reference worker `.so` — minimal plugin proves dlopen embed path end-to-end
- [x] `std::execution` sender adaptor — `iris::just(val) | iris::via(backend) | iris::then(f)`
- [x] `std::meta` — derive `TypeDescriptor` without listing fields manually (C++26)
- [x] `PrimitiveKind::CStr` — char[N] fields are null-terminated strings, not raw bytes
- [x] `by_name_` shadowing fix — second registration with same name rejected
- [x] `memory_order_relaxed` → `memory_order_acquire` in freeze check
- [x] TypeId includes field offsets — padding differences produce distinct TypeIds
- [x] `sdk/py` `KIND_CSTR = 10` — Python SDK has the CStr constant

### irsh language

- [x] Parser — tokenise and parse irsh syntax into AST; all constructs from IRSH.md
- [x] Type checker — field names resolved against TypeRegistry at parse time
- [x] Pipeline executor — walk typed AST, build IrisGen chain, pull lazily
- [x] Session variables — `let x = expr`; lazy pipeline or materialised Vec
- [x] Lazy evaluation — `OsStream::recv()` pulls one element per call; `let x = ls` = zero syscalls
- [x] Empty-write safety — `write "file.txt"` opens file only on first value received
- [x] `collect` — materialises `LazyStream<T>` into `Vec<T>` in session memory
- [x] `./path` as source or stage — sugar for `@os.exec(path)`
- [x] `@base.type(Name)` — print field list of a named type
- [x] `types` / `@base.types` — enumerate all registered types
- [x] `@os.exec` / `./path` — fork+pipe, stdout as TextLine stream
- [x] `@os.clear` — clear terminal (ANSI escape, no subprocess)
- [x] BackendRegistry — dynamic dispatch via `IrshBackend` vtable; plugins slot in without recompile
- [x] Plugin `.so` discovery — startup scans `~/.iris/plugins/*.so`, dlopen, wraps C ABI

### irish interpreter

- [x] Three-mode detection — `isatty(stdin)` decides REPL / script / pipeline-component
- [x] REPL loop — replxx with history, syntax highlighting, autosuggestions, tab completion
- [x] Script runner — reads `.irsh` file, executes line by line with `\` continuation
- [x] `ExecMode::Script` — script pipelines require explicit `print`/`write` sink; REPL auto-prints
- [x] `-e "expr"` — evaluate single expression from command line
- [x] Syntax highlighting — token colours in the REPL via replxx highlighter callback
- [x] Tab completion — `@ns.op`, stage names, field names from TypeDescriptor
- [x] History hints — ZSH autosuggestions style: grey suffix from most recent matching history entry
- [x] Operator hints — after `filter <field> ` suggests `==`, `!=`, `contains`, etc.
- [x] Dynamic prompt — current directory (abbreviated), git branch, ANSI colours
- [x] `print` — renders IrisValue using TypeDescriptor field names and types
- [x] `:types` / `:type Name` REPL meta-commands — inspect registered types
- [x] `:lex` REPL meta-command — dump token stream for debugging

---

## Now — Iris engine

- [ ] `IpcBackend` zero-copy recv — map incoming payload into shared `IrisBuffer` without intermediate copy
- [ ] Abstract OS layer — platform guards so `ls`/`ps` compile on macOS and Windows
- [ ] CI matrix — Linux passing; add macOS and Windows runners

---

## Now — irsh language

- [ ] `exec( cmd $var arg )` — exec-expression DSL: own parser inside `exec()`, `$var` expands to typed value, argv built directly, no shell involved; eliminates argument injection by construction (#14)
- [ ] `$args` — positional and named arguments for scripts
- [ ] `??` fallback value — `expr ?? default`; requires error channel in pipeline
- [ ] `?|` fallback pipeline — on error, switch to alternate source
- [ ] `&` parallel pipelines — `when_all` fan-out; requires threading + join
- [ ] `&!` fire-and-forget — `schedule_on(thread_pool)` without sync_wait
- [ ] Schema evolution detection — IPC connect compares TypeId; reports differing fields by name

---

## Now — irish interpreter

- [ ] `irish-daemon` — persistent process over IPC socket; loads backends once, serves script invocations; solves per-invocation plugin reload overhead
- [ ] `$stdin` — read wire-format frames from stdin as typed IrisValue stream
- [ ] Stdout mode — text to tty; wire format to pipe (auto via `isatty(stdout)`)
- [ ] Exit codes — 0 success / 1 runtime error / 2 parse error / 3 backend unavailable
- [ ] Shebang support — `#!/usr/bin/env irish`
- [ ] `lines` / `run` — fork+execvp only, no shell, no popen (shell injection safety)
- [ ] Error messages — field name + kind + TypeDescriptor context in output

---

## Security

- [x] Frame size limit in `IpcBackend::recv()` — reject frames > 64 MiB (#9)
- [x] `by_name_` shadowing fix — plugins cannot shadow system types (#10)
- [x] `memory_order_relaxed` → `memory_order_acquire` in freeze check
- [x] `TypeRegistry::global().freeze()` called before first irsh statement (#11)
- [x] `@os.exec` / `@os.run` use fork+execvp, never popen — no shell metacharacter interpretation (#13)
- [ ] `exec( cmd $var )` DSL — argument injection impossible by construction: `$var` expands to a typed scalar, argv built as `[]string`, no shell ever invoked (#14)
- [ ] IPC socket auth — SO_PEERCRED or challenge-response; prerequisite for multi-tenant use
- [ ] FNV-64 → connection-layer auth for adversarial IPC (#12)

---

## Ecosystem

- [x] Plugin `.so` discovery — startup scans `~/.iris/plugins/*.so`
- [ ] `@ipc.*` irsh backend — expose IPC transport as a pipeline source/sink
- [ ] `@java.*` irsh backend — expose JavaBackend as a pipeline source/sink
- [ ] Java utility set discovery — `irish --classpath ./my.jar` scans classes, builds TypeDescriptors
- [ ] Rust SDK (`sdk/rs/`) — safe wrapper over C ABI via `bindgen`
- [ ] Go bindings (`sdk/go/`) — CGO wrapper for `iris_type_register` + backend vtable

---

## Architecture — intentional trade-offs

These are known design limitations with documented rationale.
Changing them is possible but requires broad refactoring.

- **Dynamic strings (offset layout)** — current `CStr[N]` is fixed-size and wastes memory for short
  strings; truncates long paths. Alternative: FlatBuffers-style offset header (4-byte offset + length
  into a tail region). Would support arbitrary-length strings and true zero-copy IPC.
  Trade-off: C++ backend authors must call an accessor instead of reading `struct.name` directly.
  Current choice: maximum C++ simplicity.

- **Packed alignment** — `TypeId` includes field offsets computed by the compiler. Padding rules
  differ between x86_64 and ARM, making TypeIds platform-specific. Alternative: require
  `#pragma pack(1)` / `__attribute__((packed))` in TypeDescriptor layout so TypeId is
  content-addressable across architectures. Trade-off: unaligned reads are slow on some CPUs.
  Current choice: native CPU speed over cross-platform binary compatibility.

- **Type namespacing** — all types share a single flat `by_name_` map; two plugins registering
  `Frame` collide. Alternative: namespace-qualified names (`@ffmpeg.Frame`, `@audio.Frame`) at
  the TypeRegistry level. Trade-off: resolver complexity, heavier TypeId hash (must include ns).
  Current choice: simple flat registry sufficient for MVP plugin set.

---

## Far

- [ ] Nested struct types — `FieldDesc::nested_id`; path expressions `filter transform.pos.x > 0`
- [ ] Session registry — `type Name { ... }` in irsh registers into session, never global
- [ ] `parse T` — convert `LazyStream<TextLine>` into `LazyStream<T>` where T is a session type
- [ ] `WasmBackend` — mirror of JavaBackend for wasmtime/wasmer
- [ ] `RubyBackend` — load libruby, reflect `rb_cObject` fields
- [ ] FFM backend for Java 22+ — zero-copy `MemorySegment`, replaces JNI path
- [ ] Embedded runtime — optional minimal JVM (GraalVM native-image or Avian)
- [ ] IR and retranslation — compile irsh to portable IR; interpret, retranslate, or execute remotely
- [ ] `@ffmpeg.*` backend — `@ffmpeg.read "in.mp4" | @ffmpeg.decode | @ffmpeg.encode | @ffmpeg.write "out.mp4"`
- [ ] `@git.*` backend — typed stream of commits, diffs, refs
- [ ] `@k8s.*` backend — typed stream of pods, services, deployments
