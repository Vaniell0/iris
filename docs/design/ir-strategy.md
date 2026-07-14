# IrisIR — portable execution strategy

This document is the plan for the intermediate representation and its
lowerings. It is **design**, not committed roadmap: any item here becomes
concrete only when a ROADMAP entry links back to a specific stage.

The goal in one line: `irsh` should be authorable as a scripting language
and deployable as a native binary, a WASM module, or a remote job — from
the same source and without a second toolchain.

---

## Why an IR

Today the interpreter walks the type-checked AST directly. That is the
right choice for the MVP — one representation, one evaluator, no lowering
step to break. It also caps portability at whatever the interpreter binary
runs on.

Three concrete needs push toward an IR layer:

1. **Portable artefacts.** `irsh` scripts should ship as a single file
   that runs anywhere. LLVM (native) and WASM (universal sandbox) are the
   two mature targets. Both consume an IR, not an AST.

2. **Remote execution.** Sending a typed pipeline to another host means
   sending something both sides agree on. The AST is a C++ data structure;
   the IR is a stable, serialisable format with a wire representation.

3. **Retranslation of hot stages.** A `filter size > 1024` running a
   million times is a candidate for a native code path. Retranslation
   needs an IR because the source AST is entangled with parser state.

An IR is not needed for correctness. It is needed to keep the language's
promises (type safety, empty-write guarantees, wire-safe types) intact
across platforms and delivery formats without recompiling irish for each.

---

## Design principles

- **Immutable and content-addressable.** Same script → same IR bytes. A
  hash of the IR is a version identifier for the program.
- **Type-carrying at every node.** Each IR node carries the `TypeId` of
  its input stream and its output. No type inference at run time.
- **Serialisable using the same wire header shape.** IR frames reuse
  the header layout (`u64 tag + u32 size + payload`) that
  `IrisValue` frames already use, so a receiver has one reader for
  both. The tag namespace is separate — IR node tags live above the
  reserved TypeId range. Chosen, not free.
- **Never regress the source.** IR is a lowering target, not a source
  language. Everything expressible in irsh is expressible in IR; the
  reverse need not hold.
- **One IR, many lowerings.** Interpreter, LLVM, WASM, remote — same IR,
  different back ends.

---

## What LLVM automates and what it doesn't

The IR + LLVM lowering chain is not "full processor automation" —
that phrase overstates what any compiler gives you. Concretely:

### What LLVM gives us automatically

- **Multi-target code generation** — one `--target=<triple>` flag
  produces x86_64, aarch64, riscv64, wasm32 binaries from the same
  IR. No per-arch source branches.
- **ABI compliance** — System V vs Windows x64, ARM AAPCS, WASI —
  calling conventions and struct-passing rules are LLVM's job.
- **Register allocation and instruction scheduling** — target-specific
  and tuned by CPU model (`-march=native`, `-mcpu=cortex-a72`, …).
- **Basic autovectorisation** — LLVM's loop vectoriser handles
  straight-line loops over primitive types without hints.
- **Link-time optimisation** — cross-module inlining, dead-code
  elimination when built with `-flto`.
- **Sanitiser support** — `-fsanitize=address,undefined,thread`
  drop in behind the same target.

### What LLVM does NOT do for us

- **Choose the right backend.** `@os.exec` vs `@ipc` vs `@java` is a
  design decision the user makes at the source level.
- **Design cache-friendly data layouts.** `TypeDescriptor` already
  forces DOD (flat structs, no pointer chases). LLVM optimises given
  the layout; it does not rewrite it.
- **Emit SIMD intrinsics beyond autovec.** If you need explicit
  `Vec4<f32>` operations, that has to appear as a first-class
  primitive in `PrimitiveKind` and be lowered explicitly.
- **Offload to GPU / TPU / accelerator.** No GPU backend exists;
  adding one is a separate stage-6 conversation.
- **Distribute across nodes.** Stage 4 (remote) covers this
  explicitly and requires a scheduling story LLVM has nothing to say
  about.
- **Prove pipeline correctness beyond types.** LLVM does not know
  our empty-write guarantee, wire-safety, or freeze semantics. Those
  invariants are enforced by the irsh checker before IR lowering.

### The honest bumper sticker

IR + LLVM makes an irsh script **portable and native-fast on
mainstream CPUs**. It does not turn "write anything" into "runs on
anything". The design remains what it always was — typed pipelines
of registered backends — and the compiler handles the parts about
which reasonable people no longer argue.

---

## Use cases with real-world analogues

The strategy is not speculative. Every stage below has an existing
peer that shipped a similar idea successfully. Iris borrows the
pattern; the differentiator is the content-addressed type system.

### 1. Ship a typed CLI as a native binary

Data engineer writes `analyze.irsh` (30 lines). `irish --compile
--target=x86_64-linux-gnu -o analyze` produces a ~2 MB static binary
that runs on any glibc box, without `irish` installed.

- **Peer:** Deno `deno compile`, Bun `bun build --compile`, PyOxidiser.
- **Difference:** typed pipeline guarantees are preserved at binary
  boundary; script cannot silently corrupt data because the compiler
  refuses non-wire-safe transports at parse time.

### 2. Cross-arch dev-to-prod

Author on macOS arm64. Ship to Linux x86_64 servers. Same source,
different `--target`. `TypeId` includes field offsets, so struct
layout is deterministic per target and communicates cleanly across
architectures.

- **Peer:** Rust's cross-compilation, Go's `GOARCH`/`GOOS`.
- **Difference:** the type registry is content-addressed, so two
  binaries agree on `TypeId` without a shared schema file — even
  when compiled on different machines by different people.

### 3. WASM-sandboxed execution for untrusted scripts

Analyst submits a filter. Server compiles to WASM, runs under
wasmtime with a capability whitelist (`@os.ls` allowed, `@os.exec`
denied). Untrusted code cannot escape.

- **Peer:** Cloudflare Workers, Fastly Compute@Edge, Shopify Functions.
- **Difference:** bash / PowerShell / nushell have no such sandbox
  story. WASI + irsh + capability-tagged backends is the whole
  answer, not a wrapper.

### 4. Move code to data (remote pipeline)

Data lives in `warehouse.corp:5432`. User writes
`filter size > 1G | sort by: date | @math.avg`. `irish --remote
warehouse` ships the IR (a few kilobytes) instead of pulling
100 GB across the network. Result stream comes back.

- **Peer:** Apache Spark job submit, DuckDB `httpfs`, Substrait, IPFS
  compute.
- **Difference:** irsh's IR is small (bytecode-free), typed, and
  content-addressed. Cache is trivial; two identical queries hit
  the same result plan.

### 5. IDE tooling on the same IR

VSCode / Emacs LSP consumes IrisIR (not the parse tree) for hover,
go-to-def, refactor. The interpreter and the IDE share one source of
truth.

- **Peer:** rust-analyzer (HIR), Roslyn (Microsoft C#), IntelliJ Rust
  plugin.
- **Difference:** IrisIR is small enough that even a lightweight
  editor plugin can consume it directly, no JVM-scale IDE required.

### 6. Reproducible builds and content-addressed cache

`.iir` bytes change iff the semantic script changes. Nix / Bazel /
Turbo cache is bit-precise. CI never re-runs an unchanged pipeline.

- **Peer:** Nix derivations, Bazel remote cache, Turborepo.
- **Difference:** the cache key is the script itself, not a hash of
  its file plus dependencies plus toolchain. Toolchain changes do
  not invalidate content-identical scripts.

### 7. LLM-generated typed pipelines

Codegen models emit `.iir` directly. The IR is a compact target
with a small vocabulary of node kinds; type errors are caught by
the hash of `TypeId`s not resolving. Free-form text generation of
irsh is possible but the IR route is more reliable.

- **Peer:** structured LLM outputs (OpenAI JSON mode, Anthropic tool
  use), Codex-style TypeScript emission checked by `tsc`.
- **Difference:** the receiver is one deterministic executor, not
  an OS shell. Failure modes are enumerable.

### 8. Static policy enforcement in CI

SRE writes: "no `@os.exec` in production scripts". CI walks `.iir`
files (a tree of tagged nodes) and rejects offenders. Simpler than
parsing bash.

- **Peer:** OPA / Rego for k8s, ESLint rules, SAST tools like Semgrep.
- **Difference:** the target of analysis is a small, typed IR, not
  a full programming language. Policies are one page instead of a
  library.

### 9. Kernel-style embedded execution (long-term)

A restricted IR subset — no `@os.exec`, bounded loops, no
allocation — could execute in tight environments (an audit hook, a
database trigger, a policy evaluator).

- **Peer:** eBPF in the Linux kernel, Wasm in Envoy filters, WASM in
  Postgres extensions.
- **Difference:** we already have the "restricted" property by
  construction — flat types, no pointers, wire-safety at parse time.
  A "userspace-eBPF-flavoured" execution mode is the smallest of
  the stages, once IR is stable.

### 10. Distributed map-reduce style (very long-term)

Compiler splits a long pipeline at parallelisable boundaries; each
fragment ships to a different worker as IR. Results merge back.

- **Peer:** Apache Beam, Dask, Ray, Google Dataflow.
- **Difference:** the plan is content-addressed IR, not a JVM
  serialised graph. Interoperates with existing wire format for the
  data itself.

---

## Sketch of IrisIR

```
Program := List<Stmt>

Stmt :=
    | Let(name, PipelineExpr)          ─ bind a pipeline to a session name
    | Type(name, TypeDescriptor)       ─ register a session type
    | Import(namespace)                ─ pull backend namespace into scope
    | Run(PipelineExpr)                ─ execute for side effect

PipelineExpr :=
    | Source(backend, op, config)               out: TypeId
    | Stage (backend, op, config, upstream)     in→out: TypeId
    | Parallel(list<PipelineExpr>)              fan-out; joins on completion
    | Fallback(primary, alternate)              primary if ok, else alternate
    | Materialise(upstream)                     LazyStream<T> → Vec<T>

Every node carries:
    in_type_id  : TypeId    (0 for sources)
    out_type_id : TypeId
    loc         : optional<{file, line, col}>   for diagnostics
```

`BackendConfig` is a variant over the same shapes the checker already
uses today (`monostate`, `string`, `Expr`, `field_list`, `SortArg`).
`Expr` inside a config (e.g. a filter predicate) is a small sub-IR of its
own: field access, comparison, and boolean combinators over scalar kinds.

A serialised IR module is a sequence of framed messages using the
existing wire header (`u64 type_id + u32 size + payload`), where the
payload is the encoded node. This means the runtime that receives an IR
frame reuses the same reader it uses for `IrisValue` frames.

---

## Staged delivery

The plan is deliberately incremental. Each stage is independently useful
and does not depend on the ones after it.

### Stage 0 — foundation (prerequisite)

- Formalise the checker's output as a typed AST distinct from the parse
  tree. Today they are the same tree annotated with types; the split
  makes the IR lowering trivial.
- Extract `PipelineExpr` construction from `Executor::run` into a
  standalone lowering function. Interpreter keeps the same behaviour.

**Deliverable:** `src/irish/ir/` with node types and lowering, no
external consumer yet. Zero user-visible change.

### Stage 1 — IR emit and re-run

- `irish --emit-ir script.irsh -o script.iir` writes the IR to disk.
- `irish --run-ir script.iir` executes without re-parsing/re-checking.
- IR bytes are content-addressable: the same source produces the same
  file byte-for-byte.

**Deliverable:** persistent, portable IR artefacts. Users can commit
`.iir` files, ship them, diff them. Startup cost of long scripts drops
because parse+check is skipped.

### Stage 2 — LLVM native lowering

- `irish --compile script.iir --target=x86_64-linux-gnu -o script`
  produces a native binary that links against `libiris.so`.
- `--target=x86_64-apple-darwin`, `--target=aarch64-linux-gnu`, etc.
  are LLVM target triples passed through unchanged.
- Inline pipeline stages become direct function calls in LLVM IR;
  `IrisValue`s stay heap-allocated for now (no scalar promotion).
- `libiris` provides the runtime symbols (`iris_type_lookup`,
  `iris_backend_call`); the binary is small (< 100 KB after strip).

**Deliverable:** a `.irsh` script becomes a distributable binary. Users
who never touched C++ ship compiled tools.

### Stage 3 — WASM lowering

- `--target=wasm32-wasi` produces a sandboxed module.
- Runtime becomes `libiris.wasm` — a WASI-compatible build with `@os.*`
  operations mapped to WASI syscalls (or unavailable, per capability).
- `@ipc` becomes a WASI socket; `@java` is disabled (JVM not available
  under WASI).
- Backends declare their WASI compatibility in their manifest; the
  compiler refuses to lower a script that uses an incompatible backend.

**Deliverable:** irsh scripts run in a browser, in a WASM edge worker,
or under wasmtime with capability control. Same source, no rewrite.

### Stage 4 — remote execution

- `irish --remote host:port script.irsh` compiles to IR locally, ships
  the IR + types to the remote irish daemon, streams typed results
  back.
- Both sides negotiate the type registry: sender enumerates TypeIds,
  receiver confirms each is known or rejects. `TypeId` being
  content-addressed makes this negotiation stateless.

**Deliverable:** `ls | filter | @ipc(host)` becomes `--remote host`,
with the same guarantees.

### Stage 5 — retranslation (JIT / AOT for hot stages)

- Runtime profiler marks pipeline stages that account for > N% of
  time. Marked stages get LLVM-lowered on the fly and swapped in.
- No user-visible change; a benchmark harness confirms the speed-up.

**Deliverable:** interpreter speed approaches compiled speed for the
paths that matter, without changing the source.

---

## Interoperability with existing pieces

- **TypeId is already the interop key.** No new registry format is
  needed for cross-runtime agreement.
- **Wire format is already the transport.** IR frames reuse the same
  header, the same `IrisBuffer` payload model, the same EOF semantics.
- **`sdk/*` C ABI is the extension point.** Native binaries call
  backends through the same C ABI plugins register today. No new
  linking model.
- **`TypeRegistry::freeze()` already gives the invariant.** IR
  assumes a frozen registry at execute time — that assumption is
  already true in the interpreter.

---

## Non-goals

- **Not a general-purpose backend framework.** IrisIR describes irsh
  pipelines. It is not a target for arbitrary languages.
- **Not a bytecode.** No instruction stream, no evaluation stack, no
  program counter. IR nodes are declarative; the executor decides how
  to walk them.
- **Not an optimising compiler in itself.** Optimisations (predicate
  fusion, loop hoisting, common-subexpression elimination) can be
  added, but the first job is correctness and portability, not
  throughput. LLVM handles the optimisations that matter at machine
  code level; higher-level rewrites are a later stage if they are
  ever justified.
- **Not a security boundary by itself.** A malicious IR can call
  `@os.exec("rm -rf /")` if the receiver has that backend enabled.
  Deployment sandboxing is the caller's responsibility (WASI is the
  intended sandbox story; capability-tagged backend registration is
  the runtime enforcement).
- **Not a distributed scheduler.** Stage 4 (remote) ships an IR to
  one named peer. Splitting a pipeline across many workers with
  autopartitioning is stage 10 material — see use case 10 above.
- **Not a replacement for the interpreter.** The interpreter is the
  default execution path and remains so. Users should not need to
  learn IR concepts to use `irsh`; IR is what makes the interpreter's
  guarantees portable, not what replaces it.

---

## What decides "start now"

Stage 0 is safe today: it's a refactor. Stage 1 becomes valuable once
`irsh` has stabilised its checker output and there are enough users to
benefit from cached artefacts. Stages 2–5 are meaningful after there is
demand for cross-platform delivery.

The right question to ask before committing to a stage is: **would
the next hundred users use this?** If the answer is "no, but the next
thousand would", the stage waits.

---

## Reading order for a new contributor

1. `docs/reference/irsh.md` — what irsh is.
2. This file — where it is going.
3. `docs/contracts/irgen-contract.md` — the boundary the interpreter
   already respects; the IR must respect it too.
4. `ROADMAP.md` — which stages have committed dates.
