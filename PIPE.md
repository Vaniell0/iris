# Iris pipe protocol

Transport specification for the Iris wire format.
Applies to: irish external process invocation, IpcBackend, and any language SDK
(Rust, Go, Python) that needs to produce or consume typed IrisValue streams.

See [IRISH.md](IRISH.md) for how irish forks and connects external processes.
See [IRIS.md](IRIS.md) for IrisValue payload variants and PrimitiveKind definitions.

How data flows from an irsh pipeline stage into an external process
and back. Read this before writing an Iris-aware utility in any language.

---

## The two pipe models

### Inline (built-in stages)

`ls | filter size > 1024 | sort by: size | print`

Everything runs inside the irsh process. No fork, no OS pipe.
`IrisValue` moves between FnBackend objects as a C++ value in memory.
The OS never sees the data — only the final `print` writes to stdout.

### External process (`ls | ./my_util`)

irsh recognises a path without a `@` sigil, forks the binary, and
connects its stdin/stdout to the pipeline via `pipe(2)`:

```
irsh process              my_util process
─────────────             ───────────────
OsBackend::ls()
    │ IrisValue
    ▼
IpcBackend::emit()  ──── stdin fd  ────►  iris::recv_stdin()
                                               │ typed struct
                                           process(entry)
                                               │ typed struct
IpcBackend::recv()  ◄─── stdout fd ─────  iris::emit_stdout()
    │ IrisValue
    ▼
next stage
```

At the OS level this is an ordinary `pipe(2)` + `fork` + `exec`.
The content flowing through the pipe is the Iris binary wire format —
not text, not JSON, not length-prefixed strings.

---

## Wire format

Every `IrisValue` on the wire is exactly:

```
┌────────────────────────────────┐
│  type_id   uint64  (8 bytes)   │  FNV-64 hash of name + field layout
├────────────────────────────────┤
│  size      uint32  (4 bytes)   │  byte length of the payload that follows
├────────────────────────────────┤
│  payload   [size bytes]        │  raw struct bytes, little-endian
└────────────────────────────────┘
```

All integers are little-endian. The payload is the struct laid out in
memory exactly as the C++ compiler placed it — no padding added or
removed by the protocol.

A stream of values is simply these frames concatenated with no separator.
End-of-stream is signalled by closing the write end of the pipe (EOF on read).

---

## Field layout inside the payload

Each field occupies `field.size` bytes at `field.offset` inside the payload.
The `TypeDescriptor` (or its wire-safe equivalent) tells you both.

### Numeric kinds (I8 / I16 / I32 / I64 / F32 / F64 / Bool)

Value stored directly at the field offset. Standard C layout, no surprises.

```
DirEntry payload (total 280 bytes, alignas(8)):
  offset  0  │ size  (int64)  │ 8 bytes, two's complement LE
  offset  8  │ mtime (int64)  │ 8 bytes
  offset 16  │ mode  (int32)  │ 4 bytes
  offset 20  │ type  (int32)  │ 4 bytes
  offset 24  │ name  (CStr)   │ 256 bytes  ← see below
```

### CStr — null-terminated string in a fixed buffer

`char name[256]` is a **CStr** field. The payload contains all 256 bytes.
The string ends at the first `\0`; bytes after it are garbage and must
be ignored.

```
Reading a CStr field:
  raw = payload[offset : offset + size]      # size bytes
  s   = raw.split(b'\0', 1)[0].decode()      # stop at first null
```

`CStr` is always self-contained inside the payload — no pointer,
no heap allocation, no indirection. This is why `DirEntry` can be
sent as a single `write()` call with no marshalling.

### Str — pointer to a heap string (rare)

`Str` fields store a raw C pointer in the payload. The pointed-to
string lives in the sender's heap and is **not** transmitted.
A Str field is not safe to use across a process boundary via IPC —
use CStr for fixed buffers or add a length-prefixed blob extension.
All built-in OS types use CStr, not Str.

### Bytes — raw binary blob

Like CStr but with no string semantics. The full `size` bytes are
meaningful. No null terminator convention.

---

## TypeId and type agreement

The sender's `type_id` in the frame header is the FNV-64 hash of the
type name and the full field layout (names, kinds, sizes in order).

The receiver must register the same struct under the same name and
field layout. If the layouts match, the TypeIds will match
**automatically across languages and compiler versions** — no
coordination needed.

```
// C++ side (sender)
IRIS_TYPE(DirEntry,
    IRIS_FIELD(DirEntry, size),   // I64
    IRIS_FIELD(DirEntry, mtime),  // I64
    IRIS_FIELD(DirEntry, mode),   // I32
    IRIS_FIELD(DirEntry, type),   // I32
    IRIS_CSTR_FIELD(DirEntry, name)  // CStr, 256 bytes
)

# Python side (receiver) — same hash produced
iris.register_type("DirEntry", [
    {"name": "size",  "kind": KIND_I64,  "offset":  0, "size": 8},
    {"name": "mtime", "kind": KIND_I64,  "offset":  8, "size": 8},
    {"name": "mode",  "kind": KIND_I32,  "offset": 16, "size": 4},
    {"name": "type",  "kind": KIND_I32,  "offset": 20, "size": 4},
    {"name": "name",  "kind": KIND_CSTR, "offset": 24, "size": 256},
])
```

If the TypeIds disagree, the receiver knows immediately (before
touching a single byte of the payload) that the schemas are out of sync.

---

## Minimal receiver in Python

```python
import socket, struct, sys
import iris  # sdk/py/iris.py

# register the type we expect to receive
DIR_ENTRY_ID = iris.register_type("DirEntry", [
    {"name": "size",  "kind": iris.KIND_I64,  "offset":  0, "size": 8},
    {"name": "mtime", "kind": iris.KIND_I64,  "offset":  8, "size": 8},
    {"name": "mode",  "kind": iris.KIND_I32,  "offset": 16, "size": 4},
    {"name": "type",  "kind": iris.KIND_I32,  "offset": 20, "size": 4},
    {"name": "name",  "kind": iris.KIND_CSTR, "offset": 24, "size": 256},
])

def recv_one(fd):
    header = fd.read(12)
    if not header: return None
    type_id, size = struct.unpack_from("<QI", header)
    payload = fd.read(size) if size else b""
    return type_id, payload

def decode_dir_entry(payload):
    size,  = struct.unpack_from("<q", payload,  0)
    mtime, = struct.unpack_from("<q", payload,  8)
    mode,  = struct.unpack_from("<i", payload, 16)
    typ,   = struct.unpack_from("<i", payload, 20)
    name   = payload[24:280].split(b"\0", 1)[0].decode()
    return {"size": size, "mtime": mtime, "mode": mode, "type": typ, "name": name}

stdin = sys.stdin.buffer
while True:
    frame = recv_one(stdin)
    if frame is None: break
    type_id, payload = frame
    if type_id == DIR_ENTRY_ID:
        entry = decode_dir_entry(payload)
        print(entry["name"], entry["size"])
```

Run as: `irsh> ls /src | ./filter.py`
irsh forks `filter.py`, connects stdin via pipe, streams DirEntry frames.

---

## Minimal receiver in Rust (sketch — sdk/rs pending)

```rust
use std::io::{self, Read, Write};

#[repr(C, align(8))]
struct DirEntry {
    size:  i64,
    mtime: i64,
    mode:  i32,
    typ:   i32,
    name:  [u8; 256],
}

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
    let stdin = io::stdin();
    let mut r  = stdin.lock();
    while let Some((_type_id, payload)) = recv_one(&mut r) {
        let entry: DirEntry = unsafe { std::ptr::read(payload.as_ptr() as *const _) };
        let name_end = entry.name.iter().position(|&b| b == 0).unwrap_or(256);
        let name = std::str::from_utf8(&entry.name[..name_end]).unwrap_or("?");
        println!("{} {}", name, entry.size);
    }
}
```

When `sdk/rs/` exists this becomes `iris::recv::<DirEntry>()` — one line.

---

## End-of-stream and errors

- Sender closes its write fd → receiver gets EOF on `read()` → clean shutdown
- If a pipeline stage errors, irsh closes the pipe early → receiver gets EOF
- Receiver closing its read fd → sender gets `SIGPIPE` / `EPIPE` on `write()` →
  irsh catches this and propagates as `IrisError::IpcDisconnected`

There is no out-of-band error channel in the pipe model.
For diagnostics, receivers may write to stderr freely — irsh does not touch fd 2.
