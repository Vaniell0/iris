// SPDX-License-Identifier: MIT
/// @file   benchmarks/src/bench_protobuf.cpp
/// @brief  Protobuf baseline: encode / decode / roundtrip of the DirEntry
///         equivalent from benchmarks/schemas/dir_entry.proto.
///
/// Same fixture as bench_iris.cpp — see docs/benchmarks/README.md for how
/// the two are compared. Protobuf must build the message per iteration
/// because in real IPC the sender does not get to reuse a message across
/// frames without extra work (and neither does Iris).

#include "fixture.hpp"
#include "dir_entry.pb.h"

#include <benchmark/benchmark.h>

#include <string>

using iris_bench::DirEntry;
using iris_bench::make_fixture;

namespace {

std::string encode(const DirEntry& e) {
    iris_bench::DirEntry pb;
    pb.set_size(e.size);
    pb.set_mtime(e.mtime);
    pb.set_mode(e.mode);
    pb.set_type(e.type);
    pb.set_name(e.name);
    std::string out;
    pb.SerializeToString(&out);
    return out;
}

DirEntry decode(const std::string& wire) {
    iris_bench::DirEntry pb;
    pb.ParseFromString(wire);
    DirEntry out{};
    out.size  = pb.size();
    out.mtime = pb.mtime();
    out.mode  = pb.mode();
    out.type  = pb.type();
    std::snprintf(out.name, sizeof(out.name), "%s", pb.name().c_str());
    return out;
}

} // namespace

// ── Single-entry cases ───────────────────────────────────────────────────────

static void BM_Protobuf_EncodeSingle(benchmark::State& s) {
    auto fixture = make_fixture(1);
    std::string wire;
    for (auto _ : s) {
        wire = encode(fixture[0]);
        benchmark::DoNotOptimize(wire.data());
    }
    s.SetBytesProcessed(s.iterations() * sizeof(DirEntry));
    s.counters["wire_size_bytes"] = benchmark::Counter(
        static_cast<double>(wire.size()),
        benchmark::Counter::kAvgIterations);
}
BENCHMARK(BM_Protobuf_EncodeSingle);

static void BM_Protobuf_DecodeSingle(benchmark::State& s) {
    auto fixture = make_fixture(1);
    const auto wire = encode(fixture[0]);
    for (auto _ : s) {
        auto e = decode(wire);
        benchmark::DoNotOptimize(e);
    }
    s.SetBytesProcessed(s.iterations() * sizeof(DirEntry));
}
BENCHMARK(BM_Protobuf_DecodeSingle);

static void BM_Protobuf_RoundtripSingle(benchmark::State& s) {
    auto fixture = make_fixture(1);
    for (auto _ : s) {
        auto wire = encode(fixture[0]);
        auto e    = decode(wire);
        benchmark::DoNotOptimize(e);
    }
    s.SetBytesProcessed(s.iterations() * sizeof(DirEntry));
}
BENCHMARK(BM_Protobuf_RoundtripSingle);

// ── Batch cases (N = 10 000) ─────────────────────────────────────────────────

static void BM_Protobuf_EncodeBatch10K(benchmark::State& s) {
    const auto fixture = make_fixture(10'000);
    for (auto _ : s) {
        std::vector<std::string> frames;
        frames.reserve(fixture.size());
        for (const auto& e : fixture) frames.push_back(encode(e));
        benchmark::DoNotOptimize(frames.data());
    }
    s.SetBytesProcessed(s.iterations() * fixture.size() * sizeof(DirEntry));
    s.SetItemsProcessed(s.iterations() * fixture.size());
}
BENCHMARK(BM_Protobuf_EncodeBatch10K);

static void BM_Protobuf_DecodeBatch10K(benchmark::State& s) {
    const auto fixture = make_fixture(10'000);
    std::vector<std::string> frames;
    frames.reserve(fixture.size());
    for (const auto& e : fixture) frames.push_back(encode(e));

    for (auto _ : s) {
        for (const auto& w : frames) {
            auto e = decode(w);
            benchmark::DoNotOptimize(e);
        }
    }
    s.SetBytesProcessed(s.iterations() * fixture.size() * sizeof(DirEntry));
    s.SetItemsProcessed(s.iterations() * fixture.size());
}
BENCHMARK(BM_Protobuf_DecodeBatch10K);

BENCHMARK_MAIN();
