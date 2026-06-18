# Iris — Roadmap

Tasks are closed as they ship. Organised by layer:
**Iris** (engine) · **irsh** (language) · **irish** (interpreter) · **Ecosystem**

A version tag is cut when a meaningful set of tasks lands together.

---

## Done

- [x] Core type registry with content-addressed TypeId (FNV-64)
- [x] IrisValue with OpaqueHandle — JNI-free core
- [x] `IrisBuffer` — zero-copy ref-counted buffer; replaces `vector<byte>`
- [x] `TypeRegistry::inspect()` — static type info without any runtime call
- [x] `TypeRegistry::freeze()` — lock registry post-init, zero-trust
- [x] `FnBackend<F>` — wrap any C++ callable as a `Backend`
- [x] `FnBackend` composition — `fn_a | fn_b` chains via `operator|`
- [x] `Channel` — thread-safe `IrisValue` queue connecting two backends
- [x] `emit` / `recv` wired to `Channel`
- [x] `IpcBackend` — carries `IrisValue` over Unix socketpair / named socket
- [x] `IpcBackend::emit` — `writev` scatter-gather (header + payload, one syscall)
- [x] `IrisBackendHandle` C ABI — vtable of C function pointers
- [x] `sdk/iris_backend.h` (MIT) — C ABI interface decoupled from GPL core
- [x] `sdk/iris_registry.h` (MIT) — C ABI for type registration; usable from Python/Rust/Go FFI
- [x] `sdk/cpp/iris.hpp` — C++ umbrella (types + macros + wrap/unwrap); IRIS_TYPE uses C ABI
- [x] `sdk/cpp/backend.hpp` — RAII C++ wrapper over C ABI, no GPL headers
- [x] `sdk/py/iris.py` — ctypes bindings for type registration; smoke-tested
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
- [x] `std::execution` sender adaptor — `iris::just(val) | iris::via(backend) | iris::then(f)`;
      `sync_wait()` synchronously, `schedule_on(thread_pool)` for async
- [x] `std::meta` — derive `TypeDescriptor` without listing fields manually (C++26)
- [x] `PrimitiveKind::CStr` — char[N] fields are null-terminated strings, not raw bytes;
      wire-safe, transparent JNI String marshalling; `IRIS_CSTR_FIELD` macro

---

## Now — Iris engine

- [ ] `IpcBackend` zero-copy recv — map incoming payload into shared `IrisBuffer`
      without intermediate copy; zero-alloc path for fixed-size types
- [ ] Abstract OS layer — platform guards so `ls`/`ps` compile on macOS and Windows
- [ ] CI matrix — Linux passing; add macOS and Windows (MinGW) runners
- [x] `sdk/py` `KIND_CSTR = 10` — Python SDK has the CStr constant
- [x] TypeId includes field offsets — padding differences produce distinct TypeIds; layout bug closed

---

## Now — irsh language

The formal type system and evaluation model are specified in IRSH.md.
These tasks implement that spec.

- [ ] Parser — tokenise and parse irsh syntax from IRSH.md into an AST
- [ ] Type inference — every expression gets a static type at parse time;
      `let x = ls "/"` → `x : LazyStream<DirEntry>`;
      `ps | select pid | head 1` → `I32`; no runtime needed to know the type
- [ ] Type checker — resolve field names against `TypeRegistry` at parse time;
      `filter pid > 0` on `DirEntry` → error before any syscall;
      `DirEntry | @ipc` with a `Str` field → wire-safety error at parse time
- [ ] Pipeline executor — walk AST, build FnBackend / IpcBackend chain, call `sync_wait`
- [ ] Session variables — `let x = ...` in `map<string, IrisValue>`;
      stream variables store a lazy cursor, not a materialised vector
- [ ] Lazy evaluation — `OsStream::recv()` pulls one element per call;
      `let x = ls "/"` runs zero syscalls at assignment time
- [ ] Errors as values — `??` and `?|` operators; every stage returns
      `expected<IrisValue, IrisError>`; errors short-circuit the chain
- [ ] Empty-write safety — `write "file.txt"` opens the file only when it
      receives the first actual value; errored or zero-item sources leave the file untouched
- [ ] Parallel `&` — `when_all(s0, s1, ...)` for fan-out pipelines
- [ ] Fire-and-forget `&!` — `schedule_on(thread_pool)` without `sync_wait`
- [ ] `collect` — force `LazyStream<T>` into `Vec<T>` in session memory
- [ ] `$args` — positional and named arguments for scripts
- [ ] Schema evolution detection — IPC connect compares incoming TypeId;
      if layout drifted, report the differing fields by name, not just a hash mismatch

---

## Now — irish interpreter

- [ ] Three-mode detection — `isatty(stdin)` decides REPL / script / pipeline component
- [ ] REPL loop — `readline` with history; run pipeline; print result
- [ ] Script runner — read `.irsh` file, execute, exit with correct code
- [ ] `$stdin` — read wire-format frames from stdin as a typed IrisValue stream
- [ ] Stdout mode — text to tty; wire format to pipe (auto via `isatty(stdout)`)
- [ ] Tab completion — suggest field names from TypeDescriptor of the current stream
- [ ] `print` — render IrisValue as human-readable text using TypeDescriptor field names
- [ ] Exit codes — 0 success / 1 runtime error / 2 parse error / 3 backend unavailable
- [ ] Shebang support — `#!/usr/bin/env irish`
- [ ] External process invocation — `ls | ./binary`: fork + `pipe(2)`, wire format on stdin/stdout
- [ ] `lines(cmd)` — wrap regular Unix tool output as `TextLine { text: CStr[1024] }` stream
- [ ] Error messages — include field name, kind, and TypeDescriptor context

---

## Security

Threat model: trusted local users, accidental mistakes. FNV-64 and TypeId
provide a strong wall against accidental layout drift. The items below close
the gaps against deliberate attack and implementation bugs.

- [x] Frame size limit in `IpcBackend::recv()` — reject frames > 64 MiB;
      prevents 4 GB OOM allocation from a single malformed frame (#9)
- [x] `by_name_` shadowing fix — second registration with same name but
      different layout is rejected; plugins cannot shadow system types (#10)
- [x] `memory_order_relaxed` → `memory_order_acquire` in freeze check —
      closes the thread race on the frozen flag
- [ ] `TypeRegistry::global().freeze()` called before first irsh statement —
      registry must be immutable during parsing; belongs in irish `main()` (#11)
- [ ] `lines(cmd)` / `run(cmd)` use fork+execvp, not popen —
      eliminates shell injection; metacharacters in cmd must be a parse error (#13)
- [ ] IPC socket auth — SO_PEERCRED or challenge-response handshake so only
      trusted processes can connect; prerequisite for multi-tenant use
- [ ] FNV-64 → connection-layer auth for adversarial IPC (#12) —
      design discussion; not needed for local trusted-user model

---

## Ecosystem

- [ ] Java utility set discovery — `irish --classpath ./my.jar` scans all classes at
      connect() time via `register_class()` / `getDeclaredFields()`; TypeDescriptors built
      on the fly; no hand-written IRIS_TYPE required; tab-completion driven by discovered fields
- [ ] Plugin `.so` discovery — at startup irish scans `~/.iris/plugins/*.so`, dlopen each,
      looks for `iris_backend_create`; third-party OS utilities slot in without recompile
- [ ] Rust SDK (`sdk/rs/`) — safe wrapper over C ABI via `bindgen`;
      `iris::recv::<T>()` / `iris::emit(val)` on stdin/stdout;
      enables typed Rust utilities composable in any irsh pipeline
- [ ] Go bindings (`sdk/go/`) — CGO wrapper for `iris_type_register` + backend vtable

---

## Far

- [ ] `WasmBackend` — mirror of `JavaBackend` for wasmtime/wasmer; bridges
      `IrisValue` into WASM linear memory so plugins running in a WASM sandbox
      receive typed messages the same way subprocess workers do
- [ ] `RubyBackend` — load libruby, reflect `rb_cObject` fields into `TypeDescriptor`;
      same lifecycle pattern as `JavaBackend`; `c_to_ruby` / `ruby_to_c` round-trip
- [ ] FFM backend for Java 22+ — zero-copy `MemorySegment`, replaces JNI path
- [ ] Embedded runtime — optional minimal JVM (GraalVM native-image or Avian)
      so JavaBackend works without a system-wide Java install;
      if no JVM found at startup JavaBackend silently disables
- [ ] IR and retranslation — compile irsh to a portable intermediate representation;
      interpret locally, retranslate to native, or execute in a remote environment;
      TypeDescriptor content-addressing provides the type safety layer
