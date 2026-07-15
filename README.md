# Iris

**Move typed values between languages without a schema file, a code
generator, or hand-written FFI.**

Two runtimes agreeing on the layout of a value that flows between
them — Java and C++, Python and Rust, a shell pipeline and a
subprocess — is the recurring problem behind every polyglot system.
Today the answers are a `.proto` you check in, a JNI header you keep
in sync by hand, or Arrow + Protobuf + PyO3 stapled together. All of
them duplicate a type you already wrote.

Iris makes that agreement automatic. You write the struct once, in
its native language, and every backend reads the same
`TypeDescriptor` at runtime.

```cpp
struct DirEntry { int64_t size, mtime; int32_t mode, type; char name[256]; };
IRIS_TYPE(DirEntry, IRIS_FIELD(DirEntry, size), IRIS_FIELD(DirEntry, mtime),
                    IRIS_FIELD(DirEntry, mode), IRIS_FIELD(DirEntry, type),
                    IRIS_CSTR_FIELD(DirEntry, name))
```

That single declaration is:

- a **TypeDescriptor** — field names, kinds, offsets, sizes — read at
  runtime by every backend without a codegen step;
- a **TypeId** — FNV-64 of `name + layout` — that another process
  computes to the same 64-bit value from its own independent
  declaration, without ever seeing this header or a schema registry;
- a **wire-safe schema** — send the raw struct through a Unix socket
  or a subprocess pipe and the receiver decodes it with a 12-byte
  frame header. Full protocol: [wire format][wf].

**The schema is the code.** FlatBuffers, but you cannot forget to
regenerate.

---

## What Iris actually is

Iris ships as three layers with distinct names, licences, and
responsibilities:

| Name      | What                                                             | Licence |
|-----------|------------------------------------------------------------------|---------|
| **Iris**  | The engine — `libiris.so`, type registry, wire format, C ABI     | GPL-2.0 (core), Apache-2.0 (OS layer), MIT (SDK) |
| **irsh**  | The typed shell language — pipelines, `let` bindings, `import @ns` | GPL-2.0 |
| **irish** | The interpreter — REPL, script runner, `.so` plugin loader       | GPL-2.0 |

You build **against Iris** (from any C-ABI language, through MIT
headers), you write **irsh**, you run **irish**. The engine has no
dependency on the language — Python, Rust, Go, and Java can bind to
`sdk/iris_registry.h` and `sdk/iris_backend.h` without touching the
GPL core or the interpreter.

Whole-project architecture: [`docs/design/architecture.md`][arch].
Vocabulary reference: [`docs/glossary.md`][glossary].

---

## 30-second demo

**In the typed shell:**

```irsh
$ irish
irsh> ls "/var/log" | filter size > 1_000_000 | sort by mtime desc | head 5
NAME             SIZE      MTIME
syslog.1         4.2 MB    2026-07-13 22:14
auth.log         2.8 MB    2026-07-13 20:01
kern.log.1       1.7 MB    2026-07-12 03:44
```

`filter size > 1_000_000` is an `i64` comparison against the `size`
field of `DirEntry`, resolved at parse time. Not `grep` on text, not
a runtime type check. Write `filter size > "big"` and the checker
rejects it before any process runs.

Tab completion is driven by real field names from the
`TypeDescriptor` — no hard-coded field list anywhere in the
interpreter.

**Across a subprocess, no schema file:**

```irsh
irsh> ls "/var/log" | ./python_filter | print
```

`python_filter` is a Mode-3 backend: register `DirEntry`, read
12-byte-header + payload frames from stdin, write frames back to
stdout, exit 0. About 30 lines of Python total — the receiver
example is in [`wire-format.md`][wf].

The Python script does not link against `libiris`, does not import a
codegen output, does not know or care that irish wrote the frames.
Both sides computed the same `TypeId` from independent `DirEntry`
declarations. If the layouts diverged, the TypeIds would diverge and
the connection would be rejected on the first frame.

---

## Why this matters

The same substrate underneath the typed shell is what makes Iris
useful outside the shell. The three modes — inline, IPC, subprocess
— all rest on the same type identity and the same wire format, so
the same `DirEntry` can move between an in-process C++ function
call, a persistent Python daemon over Unix socket, or a
fork+exec'd Go binary, with the same type contract enforcing all
three.

**Concrete embedding scenarios** (elaborated in [WHY.md][why]):

- **Database UDFs** across Postgres, DuckDB, ClickHouse — today each
  vendor has its own C API and its own type marshalling. One
  Iris-based UDF library serves all of them.
- **Plugin architectures** in editors, DAWs, CAD tools — replace
  `void* data, size_t len` with an `IrisValue` that carries its own
  schema and cannot silently drift between host and plugin.
- **ML / analytics glue** — Python front-ends and C++ cores swap
  typed tensors and metadata without the Arrow + Protobuf + PyO3
  stack.
- **Robotics topics, financial contracts, media pipelines** —
  anywhere two languages must agree on a struct layout that is
  load-bearing.

TypeId is a hash of content, not a version tag: two teams compiling
against the same struct from independent trees agree automatically,
and diverge automatically the moment either side edits a field. For
adversarial IPC where a stronger integrity guarantee is required,
Iris offers an opt-in SHA-256 fingerprint handshake — design in
[`typeid-and-integrity.md`][integrity].

---

## For whom

- **Runtime authors** passing typed values between JVM, native, and
  (planned) WASM without serialisation overhead.
- **Shell and pipeline authors** who want typed tab completion and
  inline type checking without writing a type checker.
- **Plugin-substrate builders** replacing opaque byte blobs in host /
  plugin protocols with self-describing typed values.
- **Anyone who has written JNI or a `.proto` by hand and does not
  want to again.**

---

## Build and run

```bash
nix develop                                     # GCC 16, CMake, Ninja, replxx, GTest
cmake -B build -GNinja -DIRIS_IRISH=ON
cmake --build build --target irish

./build/irish                                   # interactive REPL
./build/irish script.irsh                       # run a script
./build/irish -e 'ls | filter size > 1024 | print'
```

For a minimal core (no JVM, no OS backend, no interpreter):

```bash
cmake -B build -GNinja -DIRIS_JAVA_BACKEND=OFF -DIRIS_OS_BACKEND=OFF -DIRIS_IRISH=OFF
```

Full first-run walkthrough, including build flags, plugin discovery,
and REPL shortcuts: [`docs/getting-started.md`][gs].

---

## Where to read next

Ordered by what you probably want.

| If you are …                                        | Read this |
|-----------------------------------------------------|-----------|
| Deciding whether Iris fits your problem             | [WHY.md][why] |
| Mapping the whole project layout                    | [`docs/design/architecture.md`][arch] |
| Writing irsh scripts or embedding `irish`           | [`docs/reference/irsh.md`][irsh-ref] · [`docs/reference/irish.md`][irish-ref] |
| Understanding *why* irsh looks the way it does      | [`docs/design/language.md`][lang] |
| Using the engine directly from C++, Python, Rust, Go, Java | [`docs/reference/iris.md`][iris-ref] |
| Writing a receiver in another language              | [`docs/reference/wire-format.md`][wf] |
| Writing a backend (`@my_thing`)                     | [`ECOSYSTEM.md`][eco] |
| TypeId integrity and adversarial IPC                | [`docs/design/typeid-and-integrity.md`][integrity] |
| Portable execution — IrisIR, native binaries, WASM  | [`docs/design/ir-strategy.md`][ir] |
| What is done, what is next                          | [`ROADMAP.md`][rm] |
| Contributing, review process, licence layout        | [`CONTRIBUTING.md`][contrib] |

---

## Licence

Three-layer split, deliberately, so commercial code can embed the
SDK without inheriting copyleft:

- **Core engine** (`src/`, `include/` other than `os.hpp`) — GPL-2.0.
- **OS layer** (`src/backend/os/`, `include/os.hpp`) — Apache-2.0.
- **SDK** (`sdk/*` — C, C++, Python headers; Rust and Go planned) —
  MIT. Link against it from any codebase without contamination.

An Apache-2.0 build-dependency exception covers `stdexec`. Full
wording in [LICENSE][lic]; the licence layout is summarised in
[`architecture.md`][arch] and [`CONTRIBUTING.md`][contrib].

[wf]: docs/reference/wire-format.md
[arch]: docs/design/architecture.md
[glossary]: docs/glossary.md
[why]: WHY.md
[integrity]: docs/design/typeid-and-integrity.md
[gs]: docs/getting-started.md
[irsh-ref]: docs/reference/irsh.md
[irish-ref]: docs/reference/irish.md
[iris-ref]: docs/reference/iris.md
[lang]: docs/design/language.md
[eco]: ECOSYSTEM.md
[ir]: docs/design/ir-strategy.md
[rm]: ROADMAP.md
[contrib]: CONTRIBUTING.md
[lic]: LICENSE
