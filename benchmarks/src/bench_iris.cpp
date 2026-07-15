// SPDX-License-Identifier: MIT
/// @file   benchmarks/src/bench_iris.cpp
/// @brief  Iris encode / decode / roundtrip benchmarks against Protobuf /
///         FlatBuffers baselines. Fixture: `DirEntry` (280 bytes fixed layout).
///
/// The measurement contract:
///   Encode  — take a filled DirEntry, produce the wire frame bytes
///             (12-byte header + payload) into a heap-allocated buffer.
///   Decode  — take wire frame bytes, reconstruct DirEntry (memcpy of payload
///             into a stack struct, verified by TypeId).
///   Roundtrip — encode + decode in the same iteration.
///   Batch   — the above, N=10_000, to expose per-element amortisation.
///
/// The Iris "encode" is: allocate 12 + 280 bytes, memcpy header, memcpy payload.
/// The Iris "decode" is: read header, memcpy payload into DirEntry.
/// There is no field-tag scan, no length prefix per field, no varint.
///
/// See docs/benchmarks/README.md for methodology and interpretation.

#include "fixture.hpp"

#include <sdk/cpp/iris.hpp>
#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstring>
#include <vector>

using iris_bench::DirEntry;
using iris_bench::make_fixture;

IRIS_TYPE(DirEntry,
    IRIS_FIELD(DirEntry, size),
    IRIS_FIELD(DirEntry, mtime),
    IRIS_FIELD(DirEntry, mode),
    IRIS_FIELD(DirEntry, type),
    IRIS_CSTR_FIELD(DirEntry, name)
)

namespace {

/// Wire frame layout — 8 bytes type_id, 4 bytes size, N bytes payload.
/// Matches src/backend/ipc/ipc.cpp:IpcBackend::emit.
constexpr std::size_t kHeaderBytes = sizeof(uint64_t) + sizeof(uint32_t);

std::vector<std::byte> encode(const DirEntry& e) {
    const uint64_t type_id = static_cast<uint64_t>(iris::type_id_of<DirEntry>());
    const uint32_t size    = sizeof(DirEntry);
    std::vector<std::byte> out(kHeaderBytes + size);
    std::memcpy(out.data(),                                     &type_id, sizeof(type_id));
    std::memcpy(out.data() + sizeof(type_id),                   &size,    sizeof(size));
    std::memcpy(out.data() + kHeaderBytes,                      &e,       size);
    return out;
}

DirEntry decode(const std::byte* frame, std::size_t frame_size) {
    uint64_t type_id = 0;
    uint32_t size    = 0;
    std::memcpy(&type_id, frame,                       sizeof(type_id));
    std::memcpy(&size,    frame + sizeof(type_id),     sizeof(size));
    if (type_id != static_cast<uint64_t>(iris::type_id_of<DirEntry>()) ||
        size    != sizeof(DirEntry) ||
        frame_size < kHeaderBytes + sizeof(DirEntry))
    {
        benchmark::DoNotOptimize(type_id);
        return {};
    }
    DirEntry out;
    std::memcpy(&out, frame + kHeaderBytes, sizeof(DirEntry));
    return out;
}

} // namespace

// ── Single-entry cases ───────────────────────────────────────────────────────

static void BM_Iris_EncodeSingle(benchmark::State& s) {
    auto fixture = make_fixture(1);
    for (auto _ : s) {
        auto frame = encode(fixture[0]);
        benchmark::DoNotOptimize(frame.data());
    }
    s.SetBytesProcessed(s.iterations() * sizeof(DirEntry));
    s.counters["wire_size_bytes"] = benchmark::Counter(
        static_cast<double>(kHeaderBytes + sizeof(DirEntry)));
}
BENCHMARK(BM_Iris_EncodeSingle);

static void BM_Iris_DecodeSingle(benchmark::State& s) {
    auto fixture = make_fixture(1);
    const auto frame = encode(fixture[0]);
    for (auto _ : s) {
        auto e = decode(frame.data(), frame.size());
        benchmark::DoNotOptimize(e);
    }
    s.SetBytesProcessed(s.iterations() * sizeof(DirEntry));
}
BENCHMARK(BM_Iris_DecodeSingle);

static void BM_Iris_RoundtripSingle(benchmark::State& s) {
    auto fixture = make_fixture(1);
    for (auto _ : s) {
        auto frame = encode(fixture[0]);
        auto e     = decode(frame.data(), frame.size());
        benchmark::DoNotOptimize(e);
    }
    s.SetBytesProcessed(s.iterations() * sizeof(DirEntry));
}
BENCHMARK(BM_Iris_RoundtripSingle);

// ── Batch cases (N = 10 000) ─────────────────────────────────────────────────

static void BM_Iris_EncodeBatch10K(benchmark::State& s) {
    const auto fixture = make_fixture(10'000);
    for (auto _ : s) {
        std::vector<std::vector<std::byte>> frames;
        frames.reserve(fixture.size());
        for (const auto& e : fixture) frames.push_back(encode(e));
        benchmark::DoNotOptimize(frames.data());
    }
    s.SetBytesProcessed(s.iterations() * fixture.size() * sizeof(DirEntry));
    s.SetItemsProcessed(s.iterations() * fixture.size());
}
BENCHMARK(BM_Iris_EncodeBatch10K);

static void BM_Iris_DecodeBatch10K(benchmark::State& s) {
    const auto fixture = make_fixture(10'000);
    std::vector<std::vector<std::byte>> frames;
    frames.reserve(fixture.size());
    for (const auto& e : fixture) frames.push_back(encode(e));

    for (auto _ : s) {
        for (const auto& f : frames) {
            auto e = decode(f.data(), f.size());
            benchmark::DoNotOptimize(e);
        }
    }
    s.SetBytesProcessed(s.iterations() * fixture.size() * sizeof(DirEntry));
    s.SetItemsProcessed(s.iterations() * fixture.size());
}
BENCHMARK(BM_Iris_DecodeBatch10K);

BENCHMARK_MAIN();
