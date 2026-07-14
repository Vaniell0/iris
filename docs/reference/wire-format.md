# Iris wire format — protocol reference

The binary protocol for `IrisValue` frames on the wire. It applies to
IPC sockets (`@ipc`) and external process pipes (`./binary`,
`@os.exec`). It does not apply to inline pipelines where no
serialisation occurs.

The wire format is a stable contract. Once shipped, its layout may
be extended by adding new `PrimitiveKind` values at the end of the
enum; it may not change existing byte positions or reinterpret
existing fields.

---

## Frame layout

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

All integers are little-endian. The payload is the struct as the C
compiler laid it out — offsets and sizes taken directly from
`TypeDescriptor::fields`. The protocol adds no padding, no framing,
no null terminators between frames.

A stream is these frames concatenated with no separator.
End-of-stream is signalled by closing the write end of the pipe or
socket — the receiver gets EOF on `read()`.

---

## Field layout inside the payload

**Numeric kinds** (Bool, I8, I16, I32, I64, F32, F64): value stored
directly at `field.offset`. Standard C layout, two's complement,
IEEE 754.

**CStr**: a char array of `field.size` bytes at `field.offset`.
The string ends at the first `\0`; bytes after it are unspecified
and must be ignored by the receiver. The receiver reads exactly
`field.size` bytes and scans for the null terminator.

```
# Reading a CStr field (Python)
raw = payload[offset : offset + size]   # exactly field.size bytes
s   = raw.split(b'\0', 1)[0].decode()  # stop at first null
```

`CStr` is always self-contained inside the payload — no pointer, no
heap allocation, no out-of-band data. This is what makes it
wire-safe.

**Str**: stores a raw pointer (8 bytes on 64-bit) at `field.offset`.
The pointed-to string lives in the sender's heap and is **not**
transmitted. A frame containing a `Str` field **must never be
emitted** — the parser rejects pipelines that would do this. If
such a frame is received (from a malformed sender), the receiver
must close the connection immediately.

**Bytes**: raw binary of `field.size` bytes at `field.offset`. All
bytes are meaningful; no null-terminator semantics.

---

## TypeId agreement

The sender's `type_id` in the frame header is the FNV-64 hash of the
type name and the full field layout (name, kind, offset, size for
each field in declaration order). The receiver must register a type
with identical layout.

If the receiver has registered `DirEntry` with the same field names,
kinds, offsets, and sizes as the sender — computed by the same
FNV-64 formula — the `TypeId`s will match automatically across:

- different programming languages (C++, Python, Rust, Go);
- different compiler versions;
- different platforms (assuming the same field offsets, which the
  sender's offsets tell you);
- different processes, nodes, machines.

If the `TypeId`s disagree, the receiver knows before touching a
single byte of the payload that the schemas are out of sync. The
receiver must close the connection; it must not attempt to decode a
frame whose `TypeId` it does not recognise.

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

---

## Minimal wire receiver in Python

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

---

## Minimal wire receiver in Rust

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

---

## End-of-stream and errors

- Sender closes write fd → receiver gets EOF → clean shutdown.
- Pipeline stage errors → irsh closes the pipe early → receiver gets EOF.
- Receiver closes read fd → sender gets SIGPIPE / EPIPE → irsh
  propagates as `IrisError::IpcDisconnected` through the `expected`
  chain.

There is no out-of-band error channel. Receivers may write to stderr
freely; irsh does not touch fd 2.

---

## Relationship to other formats

The wire format is minimal on purpose — 12 bytes of header, payload
is the struct's bytes as the C compiler laid them out. Comparable
formats are heavier:

- **Protobuf, FlatBuffers, Cap'n Proto** — each carries a schema
  identifier (or requires the schema out of band) and its own
  framing. Payload is a self-describing encoding.
- **Apache Arrow IPC** — carries schema in the first frame, uses
  Flatbuffers to describe columns, targets columnar data.

Iris's frames have no schema payload — the `TypeId` is a 64-bit
handle into the receiver's registry. If the receiver has not
registered the corresponding type, the frame is unreadable. This is
the deliberate trade-off: minimal on-wire size, at the cost of a
prior agreement (implicit, via `IRIS_TYPE`, not via schema files).

---

## Compatibility guarantees

| Component                | Stability                                                   |
|--------------------------|-------------------------------------------------------------|
| Header layout (`u64 + u32`) | Permanent. Changing it breaks all peers.                 |
| `PrimitiveKind` values   | Additive. New kinds appended, existing values never move.   |
| `TypeId` algorithm       | Permanent. FNV-64 over name + full field layout.            |
| Little-endian assumption | Permanent. Big-endian machines swap on read/write.          |
| Frame size limit         | 64 MiB per frame, enforced by `IpcBackend::recv()`.         |

A receiver written today against the wire format will still read
frames from senders shipped five years from now, and vice versa —
provided both sides agree on the type's field layout.
