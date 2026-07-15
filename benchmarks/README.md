# benchmarks/

Harness for measuring Iris against Protobuf and FlatBuffers on the
same fixture (`DirEntry`, 280 bytes, fixed layout).

**Read [`docs/benchmarks/README.md`](../docs/benchmarks/README.md)
first** — that document describes what is being measured, why the
comparison is set up this way, and how to interpret the results.
This README is the mechanical how-to-run.

---

## Build

Dependencies come through the Nix dev shell:

```bash
nix develop
```

The shell provides Google Benchmark, Protobuf, FlatBuffers, and
`flatc`. Any of the three cases whose dependency is missing is
skipped silently at configure time — Iris-only measurements build
regardless.

Configure with the benchmarks flag enabled:

```bash
cmake -B build -GNinja \
    -DIRIS_BUILD_BENCHMARKS=ON \
    -DIRIS_JAVA_BACKEND=OFF \
    -DIRIS_OS_BACKEND=OFF \
    -DIRIS_BUILD_TESTS=OFF
cmake --build build --target iris_benchmarks
```

CMake prints one status line per case: `benchmarks: Protobuf case
ENABLED / DISABLED (…reason…)`.

---

## Run

```bash
mkdir -p results
./build/benchmarks/bench_iris            --benchmark_out=results/iris.json
./build/benchmarks/bench_protobuf        --benchmark_out=results/protobuf.json
./build/benchmarks/bench_flatbuffers     --benchmark_out=results/flatbuffers.json
./build/benchmarks/bench_iris_specific   --benchmark_out=results/iris_specific.json
```

Each binary is a stock Google Benchmark executable — everything in
[`--help`](https://github.com/google/benchmark/blob/main/docs/user_guide.md)
applies. Common flags:

- `--benchmark_repetitions=5 --benchmark_report_aggregates_only=true`
  to see mean / median / stddev per case.
- `--benchmark_filter=BM_Iris_.*Batch` to run just the batch cases.
- `--benchmark_out_format=csv` if you prefer CSV over JSON.

---

## Reporting numbers

Do not paste one-off numbers into README / WHY / any pitch doc.
Numbers land in [`docs/benchmarks/RESULTS_TEMPLATE.md`](../docs/benchmarks/RESULTS_TEMPLATE.md),
which records the hardware, CPU governor, dependency versions, and
build flags used. Anything outside that document is unverifiable and
will be pushed back at review.

---

## Files

```
CMakeLists.txt            Dependency probes, per-case executables
schemas/dir_entry.proto   Protobuf schema mirroring the DirEntry struct
schemas/dir_entry.fbs     FlatBuffers schema mirroring the DirEntry struct
src/fixture.hpp           Shared DirEntry definition + deterministic fixture
src/bench_iris.cpp        Iris wire format encode / decode / roundtrip / batch
src/bench_protobuf.cpp    Protobuf equivalent (same cases, same fixture)
src/bench_flatbuffers.cpp FlatBuffers equivalent (same cases, same fixture)
src/bench_iris_specific.cpp  TypeId lookup, wrap/unwrap — no cross-lib comparison
```
