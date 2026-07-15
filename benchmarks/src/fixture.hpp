// SPDX-License-Identifier: MIT
/// @file   benchmarks/src/fixture.hpp
/// @brief  Shared DirEntry fixture for encode/decode/roundtrip benchmarks.
///
/// The struct definition is a copy of sdk/cpp/os.hpp:DirEntry so the
/// benchmark file can be compiled without pulling in Iris SDK headers
/// (Protobuf and FlatBuffers cases must not depend on libiris).

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace iris_bench {

struct alignas(8) DirEntry {
    int64_t size;
    int64_t mtime;
    int32_t mode;
    int32_t type;
    char    name[256];
};
static_assert(sizeof(DirEntry) == 280,
    "fixture: DirEntry must match sdk/cpp/os.hpp layout (280 bytes)");

/// Produce N deterministic DirEntry fixtures — same values across all cases,
/// so wire-size and throughput results are directly comparable.
inline std::vector<DirEntry> make_fixture(std::size_t n) {
    std::vector<DirEntry> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        DirEntry e{};
        e.size  = static_cast<int64_t>(1024 * (i + 1));
        e.mtime = static_cast<int64_t>(1'700'000'000 + i);
        e.mode  = 0644;
        e.type  = static_cast<int32_t>(i % 4);
        // A representative 15-char filename — realistic upper bound
        // without exercising the full 256-byte tail.
        std::snprintf(e.name, sizeof(e.name), "entry_%08zu.log", i);
        out.push_back(e);
    }
    return out;
}

/// Extract the printable name of a fixture entry (used to sanity-check
/// roundtrip correctness inside the benchmarks).
inline std::string name_of(const DirEntry& e) { return std::string(e.name); }

} // namespace iris_bench
