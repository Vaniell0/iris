# Iris

**Typed values across runtimes, without glue code.**

Write your struct once. Move it between C++, Java, Python, Rust, or over
a socket, with no `.proto` file, no `flatc`, no hand-written JNI.

```cpp
struct DirEntry { int64_t size, mtime; int32_t mode, type; char name[256]; };
IRIS_TYPE(DirEntry, IRIS_FIELD(DirEntry, size), IRIS_FIELD(DirEntry, mtime),
                    IRIS_FIELD(DirEntry, mode), IRIS_FIELD(DirEntry, type),
                    IRIS_CSTR_FIELD(DirEntry, name))
```

That single declaration is:

- a **TypeDescriptor** every backend can read at runtime,
- a **TypeId** (FNV-64 of name + layout) that another process computes to
  the same 64-bit value without ever seeing this header,
- a **wire-safe schema** — send it through a Unix socket or a subprocess
  pipe and the receiver decodes it with 12 bytes of framing.

The schema is the code. FlatBuffers, but you cannot forget to regenerate.

---

## In an irsh session

```irsh
irsh> ls "/var/log" | filter size > 1_000_000 | sort by mtime desc | head 5
NAME             SIZE      MTIME
syslog.1         4.2 MB    2026-07-13 22:14
auth.log         2.8 MB    2026-07-13 20:01
kern.log.1       1.7 MB    2026-07-12 03:44
```

Every stage is typed. `filter size > 1_000_000` is an `i64` comparison
against a `DirEntry` field, caught at parse time, not by `grep` on text.

---

## The three names

| Name    | What it is                                                            |
|---------|-----------------------------------------------------------------------|
| **Iris**  | The engine — `libiris.so`, type registry, wire format, C ABI (MIT SDK) |
| **irsh**  | The language — typed pipelines, `type` declarations, no runtime type-check overhead |
| **irish** | The binary — REPL, script runner, plugin loader                        |

You build against **Iris**, you write **irsh**, you run **irish**.
`libiris` alone is useful from Python, Rust, Go, or Java — no irsh
syntax required.

See [docs/glossary.md](docs/glossary.md) for the full vocabulary.

---

## Build

```bash
cmake -B build -GNinja \
  -DIRIS_JAVA_BACKEND=ON \
  -DIRIS_OS_BACKEND=ON \
  -DIRIS_STDEXEC=ON \
  -DIRIS_STDMETA=ON        # GCC 16+; enables IRIS_REFLECT(T) zero-field-list
cmake --build build

# or, without a JVM
cmake -B build -DIRIS_JAVA_BACKEND=OFF -DIRIS_OS_BACKEND=OFF
```

With Nix: `nix develop` drops into a GCC 16 shell with all dependencies
pinned. See [docs/getting-started.md](docs/getting-started.md) for the
first-run walkthrough.

---

## Documentation

| Document | What it covers |
|----------|----------------|
| [WHY.md](WHY.md)                                           | Motivation, commercial embedding scenarios, positioning vs FlatBuffers / Protobuf / codegen |
| [docs/reference/iris.md](docs/reference/iris.md)           | Engine reference: `TypeRegistry`, `IrisValue`, backends, SDK, build options |
| [docs/reference/irsh.md](docs/reference/irsh.md)           | Language reference: types, pipelines, transport, session state |
| [docs/reference/irish.md](docs/reference/irish.md)         | Interpreter: three modes, terminal UX, plugin discovery |
| [ECOSYSTEM.md](ECOSYSTEM.md)                               | Backend authorship: inline vs IPC vs process, compatibility guarantees |
| [ROADMAP.md](ROADMAP.md)                                   | Done · Now · Far — including the IrisIR (portable execution) staged plan |
| [CONTRIBUTING.md](CONTRIBUTING.md)                         | Repo layout, style rules, review process, licence guidance |
| [docs/glossary.md](docs/glossary.md)                       | Iris vs irsh vs irish, and every term used across the docs |
| [docs/reference/wire-format.md](docs/reference/wire-format.md) | Binary wire format — 12-byte header, Python/Rust receivers, compatibility |
| [docs/design/ir-strategy.md](docs/design/ir-strategy.md)   | IrisIR — five-stage plan to native binaries, cdylibs, and WASM |

---

## Licence

- **Core engine** (`src/`, `libiris.so`) — GPL-2.0. Changes propagate.
- **OS backend layer** (`src/backend/os/`) — Apache-2.0. Ships alongside
  the core; keeps the OS layer commercially embeddable in isolation.
- **SDK** (`sdk/`) — MIT. Link against the SDK headers from any
  language, in any codebase, without contamination. This is the intended
  commercial-embedding surface: `sdk/iris_backend.h`, `sdk/iris_registry.h`,
  `sdk/cpp/iris.hpp`, `sdk/py/iris.py`, and (soon) `sdk/rs/`, `sdk/go/`.

Full details, including the Apache-2.0 build-dependency exception for
`stdexec`, are in [LICENSE](LICENSE).
