// SPDX-License-Identifier: MIT
/// @file   benchmarks/src/bench_flatbuffers.cpp
/// @brief  FlatBuffers baseline: encode / decode / roundtrip of the DirEntry
///         equivalent from benchmarks/schemas/dir_entry.fbs.
///
/// Uses `FlatBufferBuilder` per iteration for encode (matches how a real
/// producer works when messages are heterogeneous). Decode is
/// `flatbuffers::GetRoot<DirEntry>(buf)` which is nominally zero-copy —
/// we exercise the accessors so the compiler cannot elide the field reads.

#include "fixture.hpp"
#include "dir_entry_generated.h"

#include <benchmark/benchmark.h>
#include <flatbuffers/flatbuffers.h>

#include <cstring>
#include <vector>

using iris_bench::DirEntry;
using iris_bench::make_fixture;

namespace {

std::vector<uint8_t> encode(const DirEntry& e) {
    flatbuffers::FlatBufferBuilder fbb(512);
    auto name = fbb.CreateString(e.name);
    auto root = iris_bench::fbs::CreateDirEntry(fbb, e.size, e.mtime, e.mode, e.type, name);
    fbb.Finish(root);
    return std::vector<uint8_t>(fbb.GetBufferPointer(),
                                fbb.GetBufferPointer() + fbb.GetSize());
}

DirEntry decode(const std::vector<uint8_t>& wire) {
    const auto* pb = iris_bench::fbs::GetDirEntry(wire.data());
    DirEntry out{};
    out.size  = pb->size();
    out.mtime = pb->mtime();
    out.mode  = pb->mode();
    out.type  = pb->type();
    if (const auto* n = pb->name()) {
        std::snprintf(out.name, sizeof(out.name), "%s", n->c_str());
    }
    return out;
}

} // namespace

// ── Single-entry cases ───────────────────────────────────────────────────────

static void BM_FlatBuffers_EncodeSingle(benchmark::State& s) {
    auto fixture = make_fixture(1);
    std::vector<uint8_t> wire;
    for (auto _ : s) {
        wire = encode(fixture[0]);
        benchmark::DoNotOptimize(wire.data());
    }
    s.SetBytesProcessed(s.iterations() * sizeof(DirEntry));
    s.counters["wire_size_bytes"] = benchmark::Counter(
        static_cast<double>(wire.size()));
}
BENCHMARK(BM_FlatBuffers_EncodeSingle);

static void BM_FlatBuffers_DecodeSingle(benchmark::State& s) {
    auto fixture = make_fixture(1);
    const auto wire = encode(fixture[0]);
    for (auto _ : s) {
        auto e = decode(wire);
        benchmark::DoNotOptimize(e);
    }
    s.SetBytesProcessed(s.iterations() * sizeof(DirEntry));
}
BENCHMARK(BM_FlatBuffers_DecodeSingle);

static void BM_FlatBuffers_RoundtripSingle(benchmark::State& s) {
    auto fixture = make_fixture(1);
    for (auto _ : s) {
        auto wire = encode(fixture[0]);
        auto e    = decode(wire);
        benchmark::DoNotOptimize(e);
    }
    s.SetBytesProcessed(s.iterations() * sizeof(DirEntry));
}
BENCHMARK(BM_FlatBuffers_RoundtripSingle);

// ── Batch cases (N = 10 000) ─────────────────────────────────────────────────

static void BM_FlatBuffers_EncodeBatch10K(benchmark::State& s) {
    const auto fixture = make_fixture(10'000);
    for (auto _ : s) {
        std::vector<std::vector<uint8_t>> frames;
        frames.reserve(fixture.size());
        for (const auto& e : fixture) frames.push_back(encode(e));
        benchmark::DoNotOptimize(frames.data());
    }
    s.SetBytesProcessed(s.iterations() * fixture.size() * sizeof(DirEntry));
    s.SetItemsProcessed(s.iterations() * fixture.size());
}
BENCHMARK(BM_FlatBuffers_EncodeBatch10K);

static void BM_FlatBuffers_DecodeBatch10K(benchmark::State& s) {
    const auto fixture = make_fixture(10'000);
    std::vector<std::vector<uint8_t>> frames;
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
BENCHMARK(BM_FlatBuffers_DecodeBatch10K);

BENCHMARK_MAIN();
