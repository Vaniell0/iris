# irsh — language specification

irsh is a scripting language built on the Iris engine. It is bash-like in
surface syntax and unix-native in orientation, but structurally different
from every shell that came before it: every value in an irsh pipeline has a
statically known type, determined at parse time, before any process runs.

This document is the authoritative specification of the irsh language,
its type system, its evaluation model, and the full transport architecture
that governs how values move across process boundaries. It supersedes all
earlier wire format and IPC notes.

---

## Syntax model

### `@` — the registered name root

`@` is the root namespace of all registered callables. Sub-namespaces use
`.`. Configuration uses `()`. Everything is a function — `|` is
left-to-right function composition (inside-out parentheses).

```
@os.ls("/var/log")        # os namespace, ls operation, path config
@os.ps                    # os namespace, ps operation, no config
@os.run("git log")        # os namespace, run operation, command config
@os.exec("./binary --f")  # os namespace, exec operation
@java("Class.method")     # java namespace, method as config
@ipc                      # ipc backend, default socket
@ipc("./sock")            # ipc backend, explicit socket
```

If a name is unambiguous (no collision in the registry), the `@namespace.`
prefix can be dropped:

```
ls("/var/log")   →   @os.ls("/var/log")
ps               →   @os.ps
run("git log")   →   @os.run("git log")
```

`@ls` is invalid — there is no backend named `ls`, only `@os.ls`.

`|` is function composition written left to right:

```
ls("/var/log") | filter | sort | print
# identical to:
print(sort(filter(ls("/var/log"))))
```

### Pipeline aliases via `let`

Since every pipeline stage is a typed function, `let` can bind a reusable
fragment. The type checker knows the input and output types of the alias:

```
let analyze = @java("Analyzer.run") | filter score > 0.8
data | analyze | print   # analyze is a typed pipeline fragment
```

### Quote rules

`()` provides a string context. Inside a backend or function call's
parentheses, `""` are optional — the parser knows the content is a string:

```
@os.ls(/var/log)        # valid — path inside () is a string
@os.ls("/var/log")      # also valid — explicit quotes
run(git log --oneline)  # valid — barewords inside () are string tokens
run("git log")          # also valid
```

Outside `()` — in `let`, `filter` predicates, struct literals — `""` are
required to distinguish string literals from identifiers and field names:

```
filter name contains "init"    # "init" is a string — required
filter name contains init      # init would be parsed as an identifier — error
let x = "hello"                # string value
let x = hello                  # identifier hello — would be a variable lookup
```

### Path literals

Tokens starting with `/`, `./`, or `~/` are `PathLiteral` tokens. In
pipeline stage position they are sugar for `@os.exec`:

```
./my_filter              →   @os.exec("./my_filter")
./my_filter --flag       →   @os.exec("./my_filter --flag")
/usr/bin/tool            →   @os.exec("/usr/bin/tool")
```

`./` is not a dedicated irsh sigil — it is the standard Unix relative-path
prefix. The lexer recognises it as a path token because it is unambiguous
in any context where an identifier or backend name would appear.

---

## What irsh is

irsh is a **typed scripting language for system automation**. Its design
centre is the pipeline — a sequence of typed transformations chained with
`|`. The fundamental guarantee is that a type error is a parse-time error:
it is caught when you press Enter, before a single syscall happens.

This has concrete consequences:

- `ls | filter pid > 0` → error at input; `DirEntry` has no `pid` field
- `ps | @ipc` with a `Str` field in `ProcEntry` → error at input; `Str`
  is a heap pointer, not wire-safe, cannot cross a process boundary
- `ls | filter size > "1k"` → error at input; `size` is `I64`, not `Str`

Every one of these would silently succeed in bash, producing wrong output
or crashing somewhere downstream. In irsh the contract is checked, not hoped.

irsh is **interpreted**, not compiled. The parser type-checks each statement
as it is read. There is no compilation step, no intermediate binary, no
bytecode. Execution begins immediately after type-checking succeeds.

irsh is **low-level** in the sense that C is low-level: no garbage
collector, no hidden allocation, no virtual machine. Values are flat structs
in memory, described by `TypeDescriptor`s and moved through the engine as
`IrisValue` objects. The language adds a typed surface over this substrate
without inserting a runtime between the user and the metal.

irsh has **no standard library**. Every command is a backend call: `ls` is
`@os.ls`, `filter` is `@base.filter`, `sort` is `@base.sort`. These are
not language constructs — they are registered backends whose shorthands the
lexer expands. Adding `@math.sum` requires zero changes to the parser or
checker — only a new backend registration. There is no `import`, no module
system in v1. The extension model is backends: `@java("Cls.method")`,
`@ipc`, `./external_binary`.

---

## What irsh is not

irsh is not bash. The syntax is similar by design (pipelines, `let`
variables, `|` composition) but the execution model is entirely different.
Bash moves text. irsh moves typed structs. There is no `grep`, no `awk`,
no text parsing inside the pipeline — if you need to filter, you name the
field and irsh reads it directly from memory.

irsh is not a general-purpose programming language. There are no classes,
no inheritance, no closures, no first-class functions beyond what the engine
provides. irsh programs describe *what data to move and how to transform it*.
If you need a loop over arbitrary logic, you call into Java, Rust, or an
external binary.

irsh is not a virtual machine. There is no bytecode, no JIT, no heap
managed by the language. Memory is owned by the Iris engine (ref-counted
`IrisBuffer`, OS allocations, JVM GC for Java objects). irsh programs
consume memory proportional to the data in flight, not to the size of
the program.

irsh is not a network protocol. It does not speak HTTP, gRPC, or JSON.
The wire format described below is a process-local pipe mechanism between
irsh and typed programs that have registered compatible `TypeDescriptor`s.
It is not a general-purpose serialisation format.

irsh is not safe in the C++ memory-safety sense — it runs on top of Iris
which is a native C++ library. What is safe: no undefined behaviour from
pipeline errors, no memory corruption from type mismatches (TypeId prevents
that), no silent truncation of data.

---

## Type system

### The three value categories

Every irsh expression belongs to exactly one of three categories:

```
Scalar          — a single value of PrimitiveKind or a named struct type
LazyStream<T>   — a lazy cursor over values of type T, evaluated one at a time
Result<T, E>    — a value that carries either a T or an IrisError
```

`Result<T, E>` is implicit — it is not written by the user. Every pipeline
stage returns `expected<IrisValue, IrisError>` internally; the language
surfaces this as the `??` and `?|` operators. The user sees typed data
flowing, not `Result` wrappers.

### PrimitiveKind — scalar types

| Kind | C++ type | Size | Wire-safe | Notes |
|------|----------|------|-----------|-------|
| Void | — | 0 | — | empty signal |
| Bool | bool | 1 | yes | |
| I8 | int8_t | 1 | yes | |
| I16 | int16_t | 2 | yes | |
| I32 | int32_t | 4 | yes | |
| I64 | int64_t | 8 | yes | |
| F32 | float | 4 | yes | |
| F64 | double | 8 | yes | |
| Str | std::string* | 8 | **no** | heap pointer; process-local only |
| CStr | char[N] | N | yes | null-terminated in fixed buffer |
| Bytes | uint8_t[N] | N | yes | raw binary in fixed buffer |

**Wire-safety rule:** a struct type `T` is wire-safe if and only if none of
its fields have kind `Str`. This is checked at parse time, not at the point
of transmission.

### Struct types

A struct type is identified by its `TypeDescriptor` — a name, a list of
named fields with kinds and offsets, and a total size. The `TypeId` is an
FNV-64 hash of the name and full field layout (name, kind, offset, size for
each field in order). Two structs with the same fields but different compiler
padding produce different `TypeId`s.

irsh works with two type registries:

- **Global registry** (`TypeRegistry::global()`) — populated at engine
  startup via `IRIS_TYPE` macros or `iris_type_register` from any SDK,
  then **frozen** before the first irsh statement runs. System types live
  here: `DirEntry`, `ProcEntry`, `EnvEntry`, and any C++ structs registered
  by loaded plugins or backends.

- **Session registry** (`TypeRegistry::session()`) — live throughout the
  session. Types declared with the `type` keyword in scripts or the REPL
  are stored here. Session types get the same FNV-64 TypeId as if they were
  registered in C++, so they are wire-compatible with any peer that declares
  an identical layout. The session registry has the same `by_name_` conflict
  protection: you cannot shadow a global type by name.

```irsh
type Commit { hash: CStr[41], msg: CStr[256], ts: I64 }
# Commit is now available in this session's pipelines
```

Session types are ephemeral — they do not survive process restart and are
not visible to C++ code at compile time.

### Type inference

The type of every expression is determined statically by the parser:

```
# Source commands
ls "path"           : LazyStream<DirEntry>
ps                  : LazyStream<ProcEntry>
env                 : LazyStream<EnvEntry>
Point { x: 3, y: 4 }: Point

# Pipeline operators
S : LazyStream<T>
─────────────────────────────────────────────────────────────────────────
S | filter expr     : LazyStream<T>      — expr must be valid for T
S | sort by: f      : LazyStream<T>      — f must be an orderable field of T
S | map { f1, f2 }  : LazyStream<Proj>  — Proj is an anonymous struct with f1,f2
S | select f        : LazyStream<Kind(f)>— extracts one scalar field
S | head n          : LazyStream<T>      — limits to n elements
S | head 1          : T                  — single value (unwrapped scalar)
S | @ipc(addr)      : Void              — T must be wire-safe
S | @java("Cls.m")  : Result<T', E>     — T' is the Java method's return type

# Variable binding
let x = expr        — x : type_of(expr)
```

`LazyStream<T>` is the default for all OS source commands. The stream is
not evaluated when the `let` is parsed — the cursor is stored. Evaluation
happens when the stream is consumed by a downstream stage.

### Type errors — when they are caught

| Error | When caught |
|-------|-------------|
| Unknown field in `filter` | Parse time — before any syscall |
| Kind mismatch in `filter` (e.g. string compared to I64) | Parse time |
| `Str` field in a type sent via `@ipc` or `./binary` | Parse time |
| TypeId mismatch at IPC receiver | Before first payload byte is read |
| Field count wrong in struct literal | Parse time |
| Unknown type name in struct literal | Parse time |
| I/O error in `ls`, `ps`, etc. | Runtime — surfaces as `IrisError` |
| Java exception in `@java(...)` | Runtime — surfaces as `IrisError` |
| Socket unavailable in `@ipc(...)` | Runtime — surfaces as `IrisError` |

The boundary is clear: structural errors are parse-time. I/O and network
errors are runtime. The user never sees a runtime error caused by a type
problem.

---

## Evaluation model

### Laziness

`LazyStream<T>` values are lazy by default. Storing a stream in a `let`
variable does not run any OS call:

```
let logs = ls "/var/log"         # no opendir() called here
let big  = logs | filter size > 4096  # no readdir() called here
big | sort by: size | print      # opendir(), readdir() calls happen here
```

Each stage in the pipeline pulls one element at a time from its source
(the CRTP `OsStream::recv()` model). Memory usage is proportional to
one element, not to the entire directory listing.

When a stream is consumed multiple times through the same variable, it is
re-opened from scratch:

```
let logs = ls "/var/log"
logs | print               # opens /var/log, reads, closes
logs | sort by: size | print  # opens /var/log again, reads, closes
```

If you want a snapshot (sorted, filtered, materialized in memory), pipe to
`collect`:

```
let snap = ls "/var/log" | filter size > 4096 | collect
```

`collect` forces evaluation and stores `Vec<IrisValue>` in the session.
The variable `snap` has type `Vec<DirEntry>`, not `LazyStream<DirEntry>`.

### Error propagation

Every stage returns `expected<IrisValue, IrisError>`. An error at any stage
propagates forward and bypasses all subsequent stages. The sink stage
(print, write, @ipc) is never called if any prior stage failed.

This is the **empty-write guarantee**: if you write

```
generate_data | write "output.txt"
```

and `generate_data` produces an error or terminates with zero values, the
`write` stage never opens `output.txt`. The file is not created, not
truncated, not touched. `write` opens the file only on receipt of the first
actual `IrisValue`. There is no race between "open the file" and "data is
coming" because both happen in the same pull-from-source loop.

More precisely: if `generate_data | write "output.txt"` is run in parallel
with another pipeline that reads `output.txt`, the reader cannot observe a
partial write, because write is serialised through the `expected` chain —
it either writes all the data or writes nothing.

Concretely:
```
# Safe — write never opens the file if the filter produces zero results
ls "/proc/nonexistent" | write "results.txt"   # ls fails → write not called

# Safe — even if filter passes zero items, write is never called
ps | filter name == "no_such_process" | write "pids.txt"  # zero items, no file open
```

### Parallel pipelines

`&` runs multiple source pipelines simultaneously on the thread pool:

```
ls / & ps & env | @java("Dashboard.render")
```

All three arms run in parallel. Their results are joined (as a single
combined stream) before being passed to `Dashboard.render`. If any arm
fails, the join produces an error and the sink is not called.

`&!` detaches a pipeline without waiting for the result:

```
ps &!      # fires and forgets — errors are logged, not surfaced
```

---

## Pipeline transport — inline vs process boundary

irsh has three transport modes. Understanding which one applies is critical
because they have different performance profiles and different safety rules.

### Mode 1 — Inline (backend call chain)

```
ls | filter size > 1024 | sort by: size | print
# expands to:
@os.ls | @base.filter size > 1024 | @base.sort by: size | @base.print
```

Everything runs inside the irish process. Each backend implements `make_gen()`,
returning a lazy pull generator — `std::function<std::optional<IrisValue>()>`.
The executor chains generators left to right: the source backend opens an OS
stream and returns a pull cursor; each transform backend wraps the upstream
cursor and applies its logic lazily; the sink (`@base.print`, `@base.write`)
pulls the chain to completion and writes output.

No OS pipe. No fork. No serialisation. Values move as `IrisValue` C++ objects —
zero copies for `IrisBuffer` (ref-counted). The OS never sees intermediate data.

### Mode 2 — IPC over Unix socket (`@ipc`)

```
ps | filter state == "R" | @ipc("./worker.sock")
```

The `IpcBackend` serialises each `IrisValue` into the wire format and
writes it to a Unix domain socket. The receiver on the other end is a
separate long-running process that has already connected and registered
compatible types.

The receiver is also an irsh pipeline — or any program that speaks the
Iris wire protocol. The connection is established before the pipeline runs
(the `@ipc` syntax resolves the socket at parse time; a connection failure
is a parse-time setup error, not a mid-pipeline surprise).

**Wire-safety check:** `@ipc` requires that the stream element type `T` is
wire-safe. If `T` contains a `Str` field, the parser rejects the pipeline.
The wire format cannot transmit heap pointers.

Socket backpressure: the IpcBackend does not buffer. If the socket's send
buffer fills (the receiver is slow), `emit()` blocks until space is
available. This is deliberate — irsh does not silently drop data.

### Mode 3 — External process via fork+exec (`./binary` or `@exec`)

```
ls /src | ./my_filter
ls /src | @exec ./my_filter --flag
```

`./path` is the short form — a path literal acting as its own sigil.
`@exec path args...` is the explicit form when you need to pass arguments;
the parser consumes tokens until the next `|` or end of statement, no
quotes or parentheses needed.

irish recognises a bare path as an external process invocation. It:

1. Forks
2. Connects stdin of the child to a `pipe(2)` write end
3. Connects stdout of the child to a `pipe(2)` read end
4. exec's the binary

Then:
- The parent (irish) serialises each `IrisValue` from the upstream pipeline
  into the wire format and writes it to the child's stdin
- The child reads frames from its stdin using the wire protocol
- The child writes typed frames to its stdout
- The parent deserialises the child's stdout frames and feeds them to the
  next stage in the pipeline

This is identical to classic Unix pipe(2), except the content is Iris wire
frames instead of text lines.

Wire-safety applies here too: the type flowing into `./binary` must contain
no `Str` fields. The type flowing out is whatever the child registers and
announces via its first frame's TypeId.

For ordinary Unix tools that produce text output (not Iris frames), irsh
provides `@os.exec`:

```
@os.exec("git status")                     : LazyStream<TextLine>
@os.exec("git log --oneline")              : LazyStream<TextLine>
@os.exec("grep -r TODO src/") | @base.filter text contains "fix" | @base.print
```

`TextLine` is a registered type `{ text: CStr[512] }`. The output of any
text-mode Unix tool can enter irsh pipelines through this shim — but the
values are `CStr` strings, not typed structs, and they gain no field-level
type checking until you parse them explicitly.

`./path` shorthand (path literal as pipeline stage) expands to `@os.exec`:

```
./my_filter          →   @os.exec("./my_filter")
/usr/bin/tool        →   @os.exec("/usr/bin/tool")
```

---

## Wire format

This section is the complete specification of the Iris binary wire format.
It applies to IPC sockets (Mode 2) and external process pipes (Mode 3).
It does not apply to inline pipelines (Mode 1) where no serialisation occurs.

### Frame layout

Every `IrisValue` on the wire is exactly:

```
┌────────────────────────────────┐
│  type_id   uint64  (8 bytes)   │  FNV-64 of name + field layout, little-endian
├────────────────────────────────┤
│  size      uint32  (4 bytes)   │  byte length of payload; little-endian
├────────────────────────────────┤
│  payload   [size bytes]        │  raw struct bytes, platform layout
└────────────────────────────────┘
```

All integers are little-endian. The payload is the struct as the C compiler
laid it out — offsets and sizes taken directly from `TypeDescriptor::fields`.
The protocol adds no padding, no framing, no null terminators between frames.

A stream is these frames concatenated with no separator. End-of-stream is
signalled by closing the write end of the pipe or socket — the receiver gets
EOF on `read()`.

### Field layout inside the payload

**Numeric kinds** (Bool, I8, I16, I32, I64, F32, F64): value stored directly
at `field.offset`. Standard C layout, two's complement, IEEE 754.

**CStr**: a char array of `field.size` bytes at `field.offset`. The string
ends at the first `\0`; bytes after it are unspecified and must be ignored
by the receiver. The receiver reads exactly `field.size` bytes and scans for
the null terminator.

```
# Reading a CStr field (Python)
raw = payload[offset : offset + size]   # exactly field.size bytes
s   = raw.split(b'\0', 1)[0].decode()  # stop at first null
```

CStr is always self-contained inside the payload — no pointer, no heap
allocation, no out-of-band data. This is what makes it wire-safe.

**Str**: stores a raw pointer (8 bytes on 64-bit) at `field.offset`. The
pointed-to string lives in the sender's heap and is **not** transmitted.
A frame containing a `Str` field **must never be emitted** — the parser
rejects pipelines that would do this. If such a frame is received (from
a malformed sender), the receiver must close the connection immediately.

**Bytes**: raw binary of `field.size` bytes at `field.offset`. All bytes
are meaningful; no null-terminator semantics.

### TypeId agreement

The sender's `type_id` in the frame header is the FNV-64 hash of the type
name and the full field layout (name, kind, offset, size for each field in
declaration order). The receiver must register a type with identical layout.

If the receiver has registered `DirEntry` with the same field names, kinds,
offsets, and sizes as the sender — computed by the same FNV-64 formula —
the TypeIds will match automatically across:

- different programming languages (C++, Python, Rust, Go)
- different compiler versions
- different platforms (assuming the same field offsets, which the sender's
  offsets tell you)
- different processes, nodes, machines

If the TypeIds disagree, the receiver knows before touching a single byte
of the payload that the schemas are out of sync. The receiver must close
the connection; it must not attempt to decode a frame whose TypeId it does
not recognise.

```
// C++ sender
IRIS_TYPE(DirEntry,
    IRIS_FIELD(DirEntry, size),   // I64, offset=0, size=8
    IRIS_FIELD(DirEntry, mtime),  // I64, offset=8, size=8
    IRIS_FIELD(DirEntry, mode),   // I32, offset=16, size=4
    IRIS_FIELD(DirEntry, type_),  // I32, offset=20, size=4
    IRIS_CSTR_FIELD(DirEntry, name) // CStr, offset=24, size=256
)

# Python receiver — same TypeId produced, automatically
iris.register_type("DirEntry", [
    {"name": "size",  "kind": iris.KIND_I64,  "offset":  0, "size": 8},
    {"name": "mtime", "kind": iris.KIND_I64,  "offset":  8, "size": 8},
    {"name": "mode",  "kind": iris.KIND_I32,  "offset": 16, "size": 4},
    {"name": "type_", "kind": iris.KIND_I32,  "offset": 20, "size": 4},
    {"name": "name",  "kind": iris.KIND_CSTR, "offset": 24, "size": 256},
])
```

### Minimal wire receiver in Python

```python
import struct, sys
import iris

DIR_ENTRY_ID = iris.register_type("DirEntry", [
    {"name": "size",  "kind": iris.KIND_I64,  "offset":  0, "size": 8},
    {"name": "mtime", "kind": iris.KIND_I64,  "offset":  8, "size": 8},
    {"name": "mode",  "kind": iris.KIND_I32,  "offset": 16, "size": 4},
    {"name": "type_", "kind": iris.KIND_I32,  "offset": 20, "size": 4},
    {"name": "name",  "kind": iris.KIND_CSTR, "offset": 24, "size": 256},
])

def recv_one(fd):
    hdr = fd.read(12)
    if not hdr: return None
    type_id, size = struct.unpack_from("<QI", hdr)
    payload = fd.read(size) if size else b""
    return type_id, payload

def decode_dir_entry(p):
    size,  = struct.unpack_from("<q", p,  0)
    mtime, = struct.unpack_from("<q", p,  8)
    mode,  = struct.unpack_from("<i", p, 16)
    typ,   = struct.unpack_from("<i", p, 20)
    name   = p[24:280].split(b"\0", 1)[0].decode()
    return {"size": size, "mtime": mtime, "mode": mode, "type": typ, "name": name}

for frame in iter(lambda: recv_one(sys.stdin.buffer), None):
    type_id, payload = frame
    assert type_id == DIR_ENTRY_ID, f"unexpected type {type_id:#x}"
    entry = decode_dir_entry(payload)
    print(entry["name"], entry["size"])
```

Run as: `irsh> ls /src | ./receiver.py`

### Minimal wire receiver in Rust

```rust
use std::io::{self, Read};

#[repr(C, align(8))]
struct DirEntry { size: i64, mtime: i64, mode: i32, typ: i32, name: [u8; 256] }

fn recv_one(r: &mut impl Read) -> Option<(u64, Vec<u8>)> {
    let mut hdr = [0u8; 12];
    r.read_exact(&mut hdr).ok()?;
    let type_id = u64::from_le_bytes(hdr[0..8].try_into().unwrap());
    let size    = u32::from_le_bytes(hdr[8..12].try_into().unwrap()) as usize;
    let mut payload = vec![0u8; size];
    r.read_exact(&mut payload).ok()?;
    Some((type_id, payload))
}

fn main() {
    let mut r = io::stdin().lock();
    while let Some((_id, payload)) = recv_one(&mut r) {
        let entry: DirEntry = unsafe { std::ptr::read(payload.as_ptr() as *const _) };
        let end = entry.name.iter().position(|&b| b == 0).unwrap_or(256);
        let name = std::str::from_utf8(&entry.name[..end]).unwrap_or("?");
        println!("{} {}", name, entry.size);
    }
}
```

When `sdk/rs/` exists, this becomes `iris::recv::<DirEntry>()` — one line.

### End-of-stream and errors

- Sender closes write fd → receiver gets EOF → clean shutdown
- Pipeline stage errors → irsh closes the pipe early → receiver gets EOF
- Receiver closes read fd → sender gets SIGPIPE / EPIPE → irsh propagates
  as `IrisError::IpcDisconnected` through the `expected` chain

There is no out-of-band error channel. Receivers may write to stderr freely;
irsh does not touch fd 2.

---

## Variables and session state

```
let x = ls "/var/log"             # x : LazyStream<DirEntry>
let p = x | filter size > 4096   # p : LazyStream<DirEntry>
let n = ps | filter name == "init" | select pid | head 1  # n : I32
```

Session is a `map<string, IrisValue>`. Variables are resolved before
pipeline execution begins. A `let` binding only takes effect if the
right-hand side type-checks successfully — failed bindings leave the
session unchanged.

Variables are lexically scoped to the session (or script block). There is
no heap lifetime issue because `IrisValue` with `IrisBuffer` payload is
ref-counted — the stream cursor holds a reference to the type descriptor,
not to the underlying data.

Re-assigning a variable replaces the old value:

```
let logs = ls "/var/log"
let logs = ls "/tmp"    # OK — rebinds logs
```

---

## Standard backends

All commands are backend calls. Shorthands are lexer sugar that expands
before parsing; the language has no built-in operations beyond `let`,
`type`, `|`, and `@ns.op(config)`.

### `@os` — OS sources

| Shorthand | Full form | Output type | Notes |
|-----------|-----------|-------------|-------|
| `ls "path"` | `@os.ls("path")` | `LazyStream<DirEntry>` | lazy; wraps opendir/readdir |
| `ps` | `@os.ps` | `LazyStream<ProcEntry>` | lazy; walks /proc |
| `env` | `@os.env` | `LazyStream<EnvEntry>` | lazy; walks environ[] |
| `./path` | `@os.exec("./path")` | `LazyStream<TextLine>` | fork+exec; text output |

### `@base` — stream transforms and sinks

| Shorthand | Full form | Input type | Output type | Notes |
|-----------|-----------|-----------|-------------|-------|
| `filter expr` | `@base.filter(expr)` | `LazyStream<T>` | `LazyStream<T>` | predicate on T fields |
| `sort by: f` | `@base.sort(by: f)` | `LazyStream<T>` | `LazyStream<T>` | full materialisation |
| `map { f... }` | `@base.map({ f... })` | `LazyStream<T>` | `LazyStream<Proj>` | anonymous projection |
| `select f` | `@base.select(f)` | `LazyStream<T>` | `LazyStream<Kind(f)>` | extract scalar field |
| `head n` | `@base.head(n)` | `LazyStream<T>` | `LazyStream<T>` | limit to n elements |
| `collect` | `@base.collect` | `LazyStream<T>` | `Vec<T>` | materialise all |
| `print` | `@base.print` | `LazyStream<T>` | — | pretty-print via TypeDescriptor |
| `write "path"` | `@base.write("path")` | `LazyStream<T>` | — | open file on first value only |
| `type Name` | `@base.type(Name)` | — | — | inspect one registered type |
| `types` | `@base.types` | — | — | list all registered types |

`@base.sort` is the only operation that must materialise the full stream.
All others are one-element-at-a-time.

### Future backends

| Backend | Example | Notes |
|---------|---------|-------|
| `@math.*` | `@math.sum`, `@math.avg` | numeric aggregates; zero parser changes to add |
| `@os.exec` | `@os.exec("cmd args")` | explicit form with full argument control |
| `@ipc` | `@ipc("./sock")` | Unix socket transport; wire-safe types only |
| `@java` | `@java("Cls.method")` | JVM bridge |
| `parse T` | `@base.parse(T)` | text → struct; *(planned)* |
| `$args` | — | script argument bindings; *(planned)* |

---

## Filter expressions

Filter predicates are resolved against the TypeDescriptor of the incoming
stream. Unknown field or kind mismatch → parse-time error.

```
filter size > 1024                    # I64 field, numeric comparison
filter name == "init"                 # CStr or Str field, equality
filter name contains ".cpp"           # CStr/Str, substring
filter name starts_with "lib"         # CStr/Str, prefix
filter name ends_with ".so"           # CStr/Str, suffix
filter name matches "lib.*\.so\.\d+"  # CStr/Str, regex
filter valid == true                  # Bool field
filter size > 1024 && mode == 0o755   # logical AND
filter size > 1024 || name == "core"  # logical OR
filter !(name contains "tmp")         # logical NOT
```

Compound filters short-circuit: the right side of `&&` is not evaluated if
the left side is false.

---

## Error handling

```
# Default — stop and print error
>> ls "/nonexistent"
error [OsBackend::ls]: no such file or directory: /nonexistent

# Fallback value
let n = ps | filter name == "init" | select pid | head 1 ?? 0

# Fallback pipeline
ps | filter name == "init" | head 1 ?| echo "not found"

# Checked in a conditional (future — irsh v2)
if ps | filter name == "sshd" | head 1 {
    echo "sshd running"
} else {
    echo "sshd not found"
}
```

`??` evaluates to the left side if it succeeded, or the right-side scalar
if it produced an error. The type of `expr ?? default` is `T` where `T` is
the type of both sides (they must agree).

`?|` runs the right-side pipeline only if the left side produced an error.

`&!` discards all errors by design — the pipeline runs on the thread pool,
its result is not awaited, and any error is logged to stderr.

---

## Startup flags

```
irsh --script foo.irsh           # run script, then exit
irsh --classpath ./plugins.jar   # add jar to JavaBackend classpath
irsh --ipc ./worker.sock         # default @ipc socket
irsh --type-check foo.irsh       # parse and type-check, then exit (no execution)
```

---

## Formal guarantees

These are invariants the irsh implementation must uphold. Any violation is
a bug in the interpreter, not a user error:

1. **Type-before-execute**: no OS call, socket connection, or process fork
   begins until the full pipeline has been parsed and type-checked successfully.

2. **Wire-safety-at-parse-time**: if a pipeline contains `@ipc`, `@exec`,
   or `./binary`, and the input stream element type contains any `Str` field,
   the pipeline is rejected at parse time with a specific error identifying
   the offending field.

3. **Empty-write safety**: a sink stage that creates or overwrites a file
   (`write "path"`) does not open, create, or truncate that file until it
   has received at least one successful `IrisValue` from its source chain.
   A pipeline that produces zero values or errors leaves the target file
   untouched.

4. **TypeId covers layout**: two values with the same TypeId have identical
   field names, kinds, offsets, and sizes. A receiver that decodes using
   its own TypeDescriptor for a matching TypeId will read every field at the
   correct offset. This is guaranteed because `compute_type_id()` folds all
   four properties (name, kind, offset, size) of every field into the hash.

5. **Lazy by default**: storing `let x = ls "/"` consumes O(1) memory
   regardless of directory size. No OS call is made at the point of binding.

6. **Error isolation**: an error in one arm of a `&` parallel pipeline does
   not affect the execution of other arms. All arms run to completion or
   individual failure; the join step collects all errors.

7. **Parse-time structural errors**: field-name errors, kind mismatches,
   and wire-safety violations are always parse-time errors. They are never
   surfaced at runtime.

---

## Full session example

```
>> type DirEntry
struct DirEntry {
    size  : I64  offset=0   size=8
    mtime : I64  offset=8   size=8
    mode  : I32  offset=16  size=4
    type  : I32  offset=20  size=4
    name  : CStr offset=24  size=256
}
total: 280 bytes  TypeId: 0x...  wire-safe: yes

>> let logs = ls "/var/log"
>> logs | sort by: size | print
...

>> ps | filter state == "R" | map { name, pid } | print
name: "kworker"  pid: 42

>> let pid = ps | filter name == "init" | select pid | head 1 ?? 0
>> pid
1

>> ls /src | ./my_filter --flag    # fork, pipe, Iris frames
...

>> ls / & ps | @java("Dashboard.render")
Dashboard { files: 142, procs: 87 }

>> ls "/nonexistent" | write "out.txt"
error [OsBackend::ls]: no such file or directory: /nonexistent
# out.txt is not created, not truncated
```
