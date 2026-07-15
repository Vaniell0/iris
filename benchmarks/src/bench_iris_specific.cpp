// SPDX-License-Identifier: MIT
/// @file   benchmarks/src/bench_iris_specific.cpp
/// @brief  Micro-benchmarks that only make sense for Iris — TypeId compute,
///         inline pipeline stage-to-stage cost, TypeRegistry lookup.
///
/// These are complementary to bench_iris.cpp; there is no equivalent case
/// in Protobuf or FlatBuffers to compare against. Reported alongside the
/// vs-baselines table in docs/benchmarks/README.md.

#include "fixture.hpp"

#include <sdk/cpp/iris.hpp>
#include <benchmark/benchmark.h>

#include <cstring>

using iris_bench::DirEntry;
using iris_bench::make_fixture;

IRIS_TYPE(DirEntry,
    IRIS_FIELD(DirEntry, size),
    IRIS_FIELD(DirEntry, mtime),
    IRIS_FIELD(DirEntry, mode),
    IRIS_FIELD(DirEntry, type),
    IRIS_CSTR_FIELD(DirEntry, name)
)

// ── TypeId — content-addressed identity ──────────────────────────────────────
//
// The TypeId of a type is computed once, at static-init, by hashing name +
// field layout. `iris::type_id_of<T>()` returns that cached value. This
// benchmark measures the cached-lookup path only; the one-shot registration
// cost is not on any hot path.

static void BM_Iris_TypeIdLookup(benchmark::State& s) {
    for (auto _ : s) {
        auto id = iris::type_id_of<DirEntry>();
        benchmark::DoNotOptimize(id);
    }
}
BENCHMARK(BM_Iris_TypeIdLookup);

// ── wrap / unwrap — the SDK path used by most in-process backends ────────────

static void BM_Iris_WrapSingle(benchmark::State& s) {
    auto fixture = make_fixture(1);
    for (auto _ : s) {
        auto v = iris::wrap(fixture[0]);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Iris_WrapSingle);

static void BM_Iris_UnwrapSingle(benchmark::State& s) {
    auto fixture = make_fixture(1);
    auto v = iris::wrap(fixture[0]);
    for (auto _ : s) {
        auto e = iris::unwrap<DirEntry>(v);
        benchmark::DoNotOptimize(e);
    }
}
BENCHMARK(BM_Iris_UnwrapSingle);

static void BM_Iris_WrapUnwrapRoundtrip(benchmark::State& s) {
    auto fixture = make_fixture(1);
    for (auto _ : s) {
        auto v = iris::wrap(fixture[0]);
        auto e = iris::unwrap<DirEntry>(v);
        benchmark::DoNotOptimize(e);
    }
}
BENCHMARK(BM_Iris_WrapUnwrapRoundtrip);

// ── Batch — 10K wrap/unwrap in a tight loop ──────────────────────────────────
//
// Mirrors bench_iris.cpp:BM_Iris_EncodeBatch10K but uses the SDK path
// (wrap/unwrap) rather than raw memcpy — the SDK path goes through
// IrisBuffer::from which involves a shared_ptr allocation per value.

static void BM_Iris_WrapBatch10K(benchmark::State& s) {
    const auto fixture = make_fixture(10'000);
    for (auto _ : s) {
        std::vector<iris::IrisValue> vs;
        vs.reserve(fixture.size());
        for (const auto& e : fixture) vs.push_back(iris::wrap(e));
        benchmark::DoNotOptimize(vs.data());
    }
    s.SetBytesProcessed(s.iterations() * fixture.size() * sizeof(DirEntry));
    s.SetItemsProcessed(s.iterations() * fixture.size());
}
BENCHMARK(BM_Iris_WrapBatch10K);

BENCHMARK_MAIN();
