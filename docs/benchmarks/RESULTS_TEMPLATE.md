# Benchmark results — template

The canonical place for actual measured numbers. Every row records
the hardware, kernel, CPU governor, dependency versions, and CMake
flags used, so any reader can reproduce.

**Numbers are empty until the harness has been run on a reference
box.** Do not paste one-off numbers here from a laptop under
throttling; either the run is reproducible or it is not published.

---

## Reference environment (fill in when running)

| Field                 | Value                                          |
|-----------------------|------------------------------------------------|
| CPU                   | e.g. AMD Ryzen 9 7950X, 16C/32T, 5.7 GHz max   |
| Cores available       | e.g. 32                                        |
| CPU governor          | e.g. `performance` (verify `cpupower frequency-info`) |
| RAM                   | e.g. 64 GiB DDR5-6000                          |
| Kernel                | e.g. Linux 6.6.9-arch1-1                       |
| Compiler              | e.g. GCC 16.0.0 (from `nix develop`)            |
| Build type            | `Release`, `-O3`, LTO OFF                       |
| Iris commit           | full SHA                                        |
| Google Benchmark      | version                                         |
| Protobuf              | version                                         |
| FlatBuffers           | version                                         |

---

## Single-entry cases

Reported: median wall time per iteration, ns. Runs: N ≥ 5 with
`--benchmark_repetitions=5 --benchmark_report_aggregates_only=true`.

| Case             | Iris | Protobuf | FlatBuffers | Notes |
|------------------|------|----------|-------------|-------|
| Encode           | —    | —        | —           |       |
| Decode           | —    | —        | —           |       |
| Roundtrip        | —    | —        | —           |       |
| Wire size (B)    | —    | —        | —           |       |

---

## Batch cases (N = 10 000)

Reported: median wall time per iteration, µs, plus items / second.

| Case             | Iris (µs, M/s) | Protobuf (µs, M/s) | FlatBuffers (µs, M/s) | Notes |
|------------------|----------------|--------------------|-----------------------|-------|
| Encode 10K       | —              | —                  | —                     |       |
| Decode 10K       | —              | —                  | —                     |       |

---

## Iris-specific micro-benchmarks

Reported: median wall time per iteration, ns.

| Case                            | Value | Notes |
|---------------------------------|-------|-------|
| `type_id_of<DirEntry>()` lookup | —     |       |
| `wrap(DirEntry)`                | —     |       |
| `unwrap<DirEntry>(v)`           | —     |       |
| Wrap batch 10K (µs)             | —     |       |

---

## Interpretation

Filled in once numbers are in, with references to
[`docs/benchmarks/README.md`](README.md#reading-a-result).

---

## Reproducing

```bash
nix develop
cmake -B build -GNinja -DIRIS_BUILD_BENCHMARKS=ON \
    -DIRIS_JAVA_BACKEND=OFF -DIRIS_OS_BACKEND=OFF -DIRIS_BUILD_TESTS=OFF
cmake --build build --target iris_benchmarks

mkdir -p results
for b in bench_iris bench_protobuf bench_flatbuffers bench_iris_specific; do
    ./build/benchmarks/$b \
        --benchmark_repetitions=5 \
        --benchmark_report_aggregates_only=true \
        --benchmark_out=results/$b.json \
        --benchmark_out_format=json
done
```

Transcribe the medians from `results/*.json` into the tables above,
then commit both the JSON files and this document.
