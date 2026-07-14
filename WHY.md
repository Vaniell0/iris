# Why Iris

---

Every runtime has its own representation of the same data.

```
C++:  struct FileEntry { int64_t size; int32_t mode; };
Java: class FileEntry  { long    size; int     mode; }
```

Moving a value between them today means choosing between: hand-written
JNI, a schema file you maintain separately from the code, or a
serialization library that copies everything twice and requires a
network framing.

None of these scale. All of them duplicate the type definition you
already wrote.

---

## The idea

A type registration is not bureaucracy. It is the moment you teach
the system how to handle that data everywhere.

Register `FileEntry` once. Iris knows the layout in C memory, the JNI
signatures for each field, and how to round-trip values between any
two runtimes without additional instructions. The type descriptor is
the knowledge — backends are just readers of it.

This means you can ask questions about a bridge before running it:
which fields will map, which will be skipped, whether the Java class
is layout-compatible with your C struct. Static inspection without
executing a single JNI call. The answer comes from the descriptor alone.

---

## Iris as a compile target

The engine is a library. `libiris.so` is a few hundred kilobytes.
Any C-ABI-speaking language can register types, call backends,
receive typed streams. The SDK headers (`sdk/*.h`) are MIT-licensed
so commercial code can link against them without inheriting copyleft.

When `irish` acquires a compile mode (see
[docs/design/ir-strategy.md](docs/design/ir-strategy.md)), the SDK
plays the second half of the story. An irsh script compiled to a
native binary — or a shared library — calls into whatever backends
were registered through the SDK. Two teams working in parallel:

- A **C++ (or Rust, Go, C) team** ships a domain backend as an
  MIT-stable ABI.
- A **data / analyst team** iterates on 30-line irsh pipelines that
  compose those backends.

The product ships as one artefact. Neither side has to touch the
other's language to iterate.

Two artefact shapes come from the same `.iir`:

```
irish --compile analyse.iir --target=x86_64-linux-gnu -o analyse
irish --compile analyse.iir --target=x86_64-linux-gnu --emit=cdylib -o libanalyse.so
```

The binary form is what Deno's `deno compile` produces — a
distributable typed-shell script that runs anywhere. The library
form is what makes Iris interesting to embedding platforms: any
host language that speaks C — Python via ctypes, Rust via
`bindgen`, Java via JNA, Go via CGO — can `dlopen` the emitted
`.so` and call the exported functions like any other C library.

The typed pipeline description becomes a callable API in every
language, without a Python-plugin nor a JVM classloader in sight.

---

## Where a commercial embedding fits

The shell is the most visible use. It is not the largest one. The
engine is a substrate for the type contracts that appear in every
polyglot system.

**Databases.** UDFs today marshal typed rows through per-vendor code
— Postgres extensions in Rust, DuckDB C++ callbacks, ClickHouse
aggregators. Iris is a neutral in-process ABI so one UDF library
can serve multiple engines.

**Game engines.** Game logic in one runtime, engine in C++. The
Doom-as-CAPTCHA example below is the toy version; the practical
version is Unity C# scripting or Unreal Blueprints, both proprietary.
Iris is the substrate for an open equivalent.

**AI and analytics pipelines.** Model outputs are typed tensors and
metadata; glue code is Python; core is C++ or Rust. Today the stack
is Arrow + Protobuf + hand-written PyO3 or JNI. Iris is one library
instead of a stack.

**Financial systems.** Strategy in Java or Python, matching engine
in C++. Type contracts are load-bearing — an off-by-one in a
`Price` field is a lawsuit. Iris's content-addressed identity means
the price format cannot drift silently: two independent
implementations compute the same `TypeId` or they do not connect.

**Plugin architectures.** Instead of `void* data, size_t len` a host
and plugin share `IrisValue`. Zed, VSCode, Blender, DAWs, and CAD
tools all currently invent their own type protocol. Iris is a
shared substrate that already understands cross-language calls.

**Media pipelines.** Frames flow between transform stages authored
in different languages. NVIDIA CUDA graphs, Apple Core Video,
GStreamer — each has its own type model. Iris is a lighter,
language-agnostic alternative.

**Edge and IoT.** `libiris` is small, has no JVM dependency, and
speaks the same wire format as its larger cousins. A
microcontroller emitting typed frames talks to a cloud service the
same way an internal C++ daemon does.

**Robotics.** ROS 2 has typed topics via IDL and a code generator.
Iris covers the same shape without the code generation step; the
struct in the source language is the topic.

The recurring pattern: two runtimes need to agree on the layout of
a value that flows between them. Iris makes that agreement
automatic instead of manual.

---

## Scenarios

**Typed shell.** Commands emit `IrisValue` instead of text. The shell
knows the type of every value in the pipeline — `ls | where size > 1mb`
is a numeric comparison on `i64`, not a grep. Tab completion is driven
by field names from the `TypeDescriptor`. Type errors are caught before
any process runs.

**JNI by hand is over.** You define your struct, call `IRIS_TYPE`, and
pass the value to `c_to_java`. Iris resolves field offsets, looks up
`jfieldID` per kind, and does the copies. The type you already wrote
is the only source of truth.

**Plugin substrate.** A host process registers the types it understands.
Each plugin loaded via `dlopen` registers its own types at init time.
The host can inspect any plugin's types, bridge values between plugins,
and reject an incompatible plugin before it runs — all through the same
registry, without knowing plugin internals.

The common case: a plugin system today passes messages as `void* data,
size_t len` — an opaque blob both sides must agree on by convention.
Replace that with `IrisValue`. The host knows the type of every message,
can route it to the right backend, and can call into Java for processing,
without any JNI in the plugin or the host. The type contract lives in the
descriptor, not in a shared header that drifts.

**In-process IPC bridge.** A C++ daemon and a Java service run in the
same process. They share typed values through `Channel` — no socket,
no serialization, no framing. The `Channel` is a typed queue; the
`TypeDescriptor` ensures both sides agree on the layout.

---

## What Iris is not

### vs FlatBuffers, Protobuf, Cap'n Proto

These solve serialisation across a network. You write a `.fbs`,
`.proto`, or `.capnp` schema file, run a code generator, and check
in the output. The schema is a separate artefact from the code that
uses it — you can forget to regenerate, and your types drift.

Iris solves **in-process FFI across runtimes** (and, secondarily,
IPC between processes). The schema is the C++ struct definition —
`IRIS_TYPE(FileEntry, ...)`. There is no `.proto`, no `flatc`, no
generated `.pb.go` to commit. The type is authored once in the
source language and read at run time by every backend.

The identity of a type — its `TypeId` — is a hash of its name and
full field layout. Two programs that declare the same struct
independently, without seeing each other's headers or a schema
registry, agree on the same `TypeId`. FlatBuffers, Protobuf, and
Cap'n Proto all require a shared schema file that both sides
consume.

Bumper sticker: **the schema is the code**. FlatBuffers, but you
cannot forget to regenerate.

### vs code generators generally

You do not run a tool, you do not check in generated files. The
descriptor is built at process startup and consulted at run time.
Nothing to check into version control; nothing to keep in sync.

### vs shells and scripting languages

**The Iris engine** is not a shell and not a scripting language. It is
the substrate — the type registry, the wire format, the backend contract.
**irsh** is the typed scripting language built on top of it. **irish** is
the interactive shell and interpreter that runs irsh. The engine does not
depend on either — you can use the C ABI directly, from Python, Rust, or
Java, without ever touching irsh syntax.

---

## Content-addressed identity

Every type has a `TypeId` derived from its name and field layout by
a deterministic hash. Two processes that define the same struct
independently will agree on its `TypeId` without any coordination.
This is the same insight as COM GUIDs, applied correctly: identity
derived from content, not from a registry or a counter.

---

## For whom

- Authors of language runtimes who need to pass typed values to
  other runtimes without serialization overhead
- Authors of shells and pipelines that want typed tab completion and
  inline type checking without writing a type checker
- Transport libraries that want a neutral type vocabulary shared
  across C++ and JVM without choosing sides
- Anyone who has written JNI by hand and does not want to again

---

## Further reading — design details

The mechanics of the two-registry model, why flat types force
Data-Oriented Design, and what the constraint enables in game-loop
and authentication scenarios (Doom-as-CAPTCHA) are separate concerns —
they live in [docs/design/dod-and-games.md](docs/design/dod-and-games.md)
so the motivation essay stays focused on positioning.
