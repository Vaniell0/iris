# TypeId and integrity

This document is the design answer to issue #12
(*"FNV-64 is not collision-resistant"*). It separates two concerns
that today are conflated: **identity** (are two peers talking about
the same type?) and **integrity** (can an adversary force two peers
to accept incompatible types under the same name?).

For the algorithm and code, see `src/registry.cpp` and
`sdk/iris_registry.h::iris_type_id_compute`. For where TypeId sits
in the wire format see [`../reference/wire-format.md`](../reference/wire-format.md).

---

## The current design

`TypeId` is FNV-64 over the ordered concatenation of the struct
name and, for each field in declaration order, the field's name,
kind, offset, and size.

```
TypeId := FNV-64( name || (name₀ ‖ kind₀ ‖ offset₀ ‖ size₀) ‖ … )
```

This choice was deliberate:

- **Fast.** ~5 ns per struct on a modern x86_64. Zero measurable
  cost at type registration.
- **Portable.** 20 lines of code in any language with an FNV
  reference implementation. Content-addressable across C++, Python,
  Rust, Go, Java without a shared library.
- **Small.** 64 bits fits in the wire header and in a pointer word.
- **Deterministic across languages.** Same struct declared
  independently in two runtimes agrees on `TypeId` without a
  registry service. This is the entire value proposition of Iris.

The tradeoff is the same one every hash inherits: FNV-64 is a
non-cryptographic hash. A dedicated attacker can construct a
collision — two distinct field layouts that hash to the same 64-bit
value. This is a real property to understand, but it is not a
scenario for honest peers.

---

## What FNV-64 buys and what it does not

### FNV-64 gives us identity

Two honest programs that declare the same struct (`DirEntry`, same
five fields, same offsets) will always compute the same `TypeId`.
That is content-addressed identity, and it is what makes irsh
pipelines composable across process and language boundaries without
a schema registry. FNV-64 is sufficient for identity because the
input space is small (a struct definition) and there is no
adversary trying to make the hash collide.

### FNV-64 does not give us integrity

If an adversary controls one of the peers on an IPC connection, they
can:

1. Choose a struct layout different from what the honest peer
   registered.
2. Compute an FNV-64 collision so their layout hashes to the same
   `TypeId` the honest peer expects.
3. Send frames that the honest peer decodes with the wrong offsets
   — reading past the buffer, misinterpreting fields, corrupting
   downstream computation.

The attack takes seconds on commodity hardware. Any deployment
where the two ends of an `@ipc` connection are not mutually
trusted is exposed. Multi-tenant hosts, third-party plugin
containers, hostile networks — these are the scenarios where
identity is not enough.

The right layer to add integrity is not the hash — it is
authentication on the transport, plus an optional stronger
fingerprint checked in a handshake before the first payload.

---

## The design — layered

**Layer 1 (unchanged): FNV-64 TypeId.** Stays as is. Wire header
still carries a 64-bit `type_id`. All existing peers keep working.
No compat break.

**Layer 2 (new, opt-in): SHA-256 fingerprint at handshake.** When
both peers opt in (`@ipc(...) as strict` in irsh; `IPC_STRICT=1` env
for external peers), the first bytes on the socket are a handshake:

```
  ┌──────────────────────────────────────────────────────────┐
  │  strict handshake — sent before any wire frame            │
  ├──────────────────────────────────────────────────────────┤
  │  magic       "IRISHS01"       8 bytes                     │
  │  type_count  uint32 LE        4 bytes                     │
  │  for each of type_count:                                  │
  │    type_id    uint64 LE      8 bytes  (FNV-64, identity)  │
  │    fp_sha256  [32] bytes    32 bytes  (SHA-256 of layout) │
  └──────────────────────────────────────────────────────────┘
```

The receiver computes SHA-256 of its own registered `TypeDescriptor`
for each `type_id` in the handshake. If any fingerprint disagrees,
the connection is closed before a single payload byte is read.

- Cost: 32 bytes per registered type, computed once per connection.
- Sender-computed fingerprints are cached in `TypeRegistry` after
  freeze — no recompute per connection.
- Receiver-side check is one SHA-256 per type in the handshake,
  amortised over the connection lifetime. Negligible for
  long-lived IPC.

**Layer 3 (existing, unchanged): OS-level auth.** `SO_PEERCRED`
gives you the peer's uid/gid on Unix sockets — that is a real
identity check for local trust boundaries. `--strict` is the
schema-level guard *in addition to* peer-cred, not a substitute.

---

## Why not just switch to a stronger hash

Considered and rejected. Two problems.

**Compatibility break.** Every registered type gets a new `TypeId`.
Every `.iir` artefact (once IR ships), every `.so` plugin, every
wire header in existing code — all get new numbers. This is a
cross-cutting change to a compatibility contract that shipped as
stable. Not worth it when the underlying attack requires an
adversary; there is no honest-peer failure mode being fixed.

**Wrong layer.** A stronger content hash still does not stop a
malicious peer from claiming a different registered layout under
someone else's name. Integrity between peers needs a *handshake*
that binds identity to a stronger proof, verifiable by the honest
end. That is what SHA-256 fingerprints at handshake time do; the
FNV-64 identity is orthogonal.

If a future scenario ever motivates a stronger `TypeId` (say,
a very large public plugin ecosystem where the birthday bound on
64-bit becomes a nuisance for *honest* collisions), the migration
path is to add a second flavour of `TypeId` (128-bit BLAKE3
truncated) as a separate, negotiated wire header variant — not to
change the value stored in the current 8-byte slot.

---

## Opt-in surface in irsh

```
@ipc("./worker.sock") as strict         # both ends must agree on fingerprints
@ipc("./worker.sock")                   # normal, identity-only
```

The parser accepts `as strict` after any `@ipc` call and threads a
flag through to `IpcBackend::connect`. If the peer does not support
the handshake (older receiver, `IPC_STRICT=0`), the connect fails
at parse time with a specific error, not silently downgrades.

For process pipelines (`./binary`, `@os.exec`), strict mode is
proposed as `./binary!` (postfix bang) — the child announces
strict-capability with a `IRIS_STRICT_SUPPORTED=1` in its
environment inherited from `irish`. Design open; will land only
when there is a demand from a specific plugin.

---

## Roadmap placement

- **Not on the critical path.** The threat model needs an
  adversarial IPC peer. For the common case (single-user desktop,
  trusted `.so` plugins, `@os.exec` on trusted binaries), Layer 1
  alone is sufficient.
- **Before multi-tenant use.** Any deployment that exposes an
  Iris socket across a security boundary (containers, remote
  hosts) needs Layer 2 before it ships.
- **Tracked in ROADMAP** under "Security", split into two entries:
  handshake protocol and irsh `as strict` syntax.

---

## What this document does not do

- It does not commit code today. `IpcBackend` currently sends only
  the wire frame stream, no handshake. Adding the handshake is a
  protocol change with an opt-in negotiation — safe, but scoped.
- It does not deprecate FNV-64. Identity stays FNV-64 for the
  foreseeable future.
- It does not attempt to secure the wire format against
  eavesdropping or replay. Those are transport concerns; use TLS
  or a signed transport underneath the IPC socket if the threat
  model demands it.

The document exists so a security-conscious reader understands
what Iris guarantees today, what it explicitly does not, and where
the design goes when the trust model demands more. The next patch
that touches this file is the ADR for the actual handshake
format — not a rewrite of the reasoning.
