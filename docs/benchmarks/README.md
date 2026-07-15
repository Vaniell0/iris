# Benchmark methodology

Why Iris is measured against Protobuf and FlatBuffers, what the
harness measures, and how to interpret the numbers when they land.

The harness itself lives in [`benchmarks/`](../../benchmarks/); this
document is the "why" and the interpretation guide. Numbers are
reported in [`RESULTS_TEMPLATE.md`](RESULTS_TEMPLATE.md) — that file
records hardware, kernel, CPU governor, dependency versions, and
build flags alongside every row.

---

## Why compare against Protobuf and FlatBuffers

Both are widely deployed, both are Google-authored (so criticism
lands on the strongest existing designs, not a strawman), and both
have a different value proposition than Iris:

- **Protobuf** — variable-length, tag-based encoding. Optimises for
  wire size and forward/backward compatibility. Requires code
  generation (`protoc`) at build time and produces intermediate
  message objects that must be built, then serialised.
- **FlatBuffers** — offset-table encoding. Optimises for zero-copy
  random access on the receiver. Requires code generation (`flatc`)
  at build time and a builder to construct the buffer.
- **Iris** — content-addressed struct identity, raw struct layout on
  the wire. Optimises for zero code generation and in-process FFI.
  No `.proto`, no `.fbs`, no build-time codegen step.

The three are not doing the same job. The comparison exists to
answer: **if you use Iris where Protobuf or FlatBuffers would
otherwise fit, what do you pay and what do you save?**

---

## Fixture

The fixture is the `DirEntry` struct from `sdk/cpp/os.hpp`:

```cpp
struct alignas(8) DirEntry {
    int64_t size;
    int64_t mtime;
    int32_t mode;
    int32_t type;
    char    name[256];
};
```

280 bytes fixed layout. `benchmarks/schemas/dir_entry.proto` and
`benchmarks/schemas/dir_entry.fbs` mirror this struct as closely as
each schema language allows. All three cases use the same generated
values (`benchmarks/src/fixture.hpp::make_fixture(N)`).

Two shapes:

- **Single entry** — one `DirEntry` per iteration. Exercises fixed
  per-call overhead (header, method dispatch, allocation).
- **Batch 10 000** — 10 000 entries per iteration. Amortises the
  per-call overhead and exposes the steady-state cost of the encode
  and decode paths.

---

## Cases

| Case                 | What it exercises                                       |
|----------------------|---------------------------------------------------------|
| Encode single        | Fill a message / struct, produce wire bytes             |
| Decode single        | Take wire bytes, reconstruct message / struct           |
| Roundtrip single     | Encode + decode chained in the same iteration           |
| Encode batch 10K     | Encode 10 000 fixture entries per iteration             |
| Decode batch 10K     | Decode 10 000 pre-encoded frames per iteration          |
| Wire size (bytes)    | Reported as a counter on the encode benchmarks          |

For Iris-only micro-benchmarks (no Protobuf / FlatBuffers analogue),
see the `bench_iris_specific` binary:

| Case                       | What it exercises                                     |
|----------------------------|-------------------------------------------------------|
| `iris::type_id_of<T>()`    | Cached TypeId lookup — the hot path in every call     |
| `iris::wrap(T)`            | SDK path: type_id + shared payload buffer allocation  |
| `iris::unwrap<T>(v)`       | SDK path: memcpy back into a T                        |
| Wrap batch 10K             | 10 000 wrap calls, exposing allocator cost            |

---

## What the harness deliberately does NOT measure

- **Network transport.** The wire format is the same whether the
  frames traverse loopback, a real socket, or a subprocess pipe. IO
  cost varies with kernel version, socket buffer sizes, congestion
  — measuring it here would attribute OS variance to library
  choice. If you need IPC-end-to-end numbers, wire it up
  application-side; the encode/decode numbers are the library
  contribution.
- **Compiler flags beyond the project defaults.** All three cases
  build under the same CMake flags (`-DCMAKE_BUILD_TYPE=Release`,
  `-O3`, LTO off). Tweaking flags per case would let the harness
  cherry-pick a favourable configuration.
- **Custom arenas.** Protobuf's arena allocator improves batch
  throughput at the cost of API changes. The comparison uses the
  default heap path because that is what appears in most codebases;
  a follow-up harness measuring arena-mode Protobuf against Iris is
  fair game but lives in a separate file so the two aren't
  conflated.

---

## Reading a result

The numbers to look at, in order:

1. **Wire size**. If Iris is much larger than Protobuf, the
   fixed-layout choice is expensive on this shape. If it is smaller
   or comparable, layout wins over varint on this shape. The
   `DirEntry` fixture with a 256-byte `name` field is a
   deliberately bad case for Iris — most of the payload is trailing
   zero bytes.
2. **Encode single**. Fixed per-call overhead. Iris does two
   memcpys (header + payload) and one heap allocation for the frame
   buffer. Protobuf builds a `Message` object, serialises it into a
   `std::string`. FlatBuffers runs a `FlatBufferBuilder` per call.
3. **Decode single**. For Iris, one memcpy back into the struct.
   For Protobuf, one `ParseFromString` plus a fresh `Message`.
   FlatBuffers claims zero-copy on decode — the numbers should
   confirm or refute.
4. **Batch 10K**. Steady-state cost. The single-entry numbers are
   dominated by allocator noise; batch is where the encoding shape
   shows up.
5. **Iris-specific**. Only interesting if the vs-baseline results
   look off — the `wrap`/`unwrap` numbers tell you whether the
   SDK path or the raw memcpy path dominates.

---

## Running

Everything in [`benchmarks/README.md`](../../benchmarks/README.md).
Once the harness produces JSON files, transcribe the median
throughput (or median wall time — pick one and stick to it) into
[`RESULTS_TEMPLATE.md`](RESULTS_TEMPLATE.md), one row per (library,
case). The template records hardware and dependency versions
alongside the numbers so the row can be re-run and re-verified.

---

## Status

The harness is scaffolded — Iris, Protobuf, and FlatBuffers cases
compile against their respective SDKs. **No results have been
recorded yet.** The first published numbers land in a follow-up
commit that runs the harness on a documented reference box and
fills in [`RESULTS_TEMPLATE.md`](RESULTS_TEMPLATE.md).

Until that commit exists, do not put throughput numbers in
`README.md`, `WHY.md`, or any external pitch.
