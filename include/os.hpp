// SPDX-License-Identifier: Apache-2.0
/// @file   include/os.hpp
/// @brief  OS command layer — directory listing, process snapshot, environment.

#pragma once

#include <sdk.hpp>
#include <expected>
#include <string_view>
#include <vector>

// ── Struct definitions ───────────────────────────────────────────────────────

struct alignas(8) DirEntry {
    int64_t size;       ///< bytes, 0 for directories
    int64_t mtime;      ///< seconds since epoch
    int32_t mode;       ///< unix permission bits
    int32_t type;       ///< 0 regular  1 directory  2 symlink  3 other
    char    name[256];  ///< basename
};

struct alignas(8) ProcEntry {
    int32_t pid;
    int32_t ppid;
    int64_t rss;        ///< resident set size in kB
    char    state[4];   ///< R/S/D/Z/T plus null
    char    name[256];  ///< process name (comm, up to 15 chars + null)
};

struct alignas(8) EnvEntry {
    char key[128];  ///< environment variable name
    char val[512];  ///< environment variable value
};

// ── Type registrations ───────────────────────────────────────────────────────

IRIS_TYPE(DirEntry,
    IRIS_FIELD(DirEntry, size),
    IRIS_FIELD(DirEntry, mtime),
    IRIS_FIELD(DirEntry, mode),
    IRIS_FIELD(DirEntry, type),
    IRIS_BYTES_FIELD(DirEntry, name)
)

IRIS_TYPE(ProcEntry,
    IRIS_FIELD(ProcEntry, pid),
    IRIS_FIELD(ProcEntry, ppid),
    IRIS_FIELD(ProcEntry, rss),
    IRIS_BYTES_FIELD(ProcEntry, state),
    IRIS_BYTES_FIELD(ProcEntry, name)
)

IRIS_TYPE(EnvEntry,
    IRIS_BYTES_FIELD(EnvEntry, key),
    IRIS_BYTES_FIELD(EnvEntry, val)
)

// ── Error type ────────────────────────────────────────────────────────────────

namespace iris::os {

/// Failure reason returned by OS commands.
enum class OsError {
    NotFound,         ///< path or resource does not exist
    PermissionDenied, ///< insufficient privileges
    ReadError,        ///< underlying read failed (e.g. /proc unavailable)
};

// ── Command declarations ──────────────────────────────────────────────────────

/// Walk @p path and return one DirEntry per visible entry (hidden entries excluded).
std::expected<std::vector<IrisValue>, OsError> ls(std::string_view path = ".");

/// Read /proc and return one ProcEntry per running process.
std::expected<std::vector<IrisValue>, OsError> ps();

/// Return one EnvEntry per process environment variable.
std::expected<std::vector<IrisValue>, OsError> env();

} // namespace iris::os
