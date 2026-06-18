// SPDX-License-Identifier: MIT
/// @file   sdk/cpp/os.hpp
/// @brief  OS command types — DirEntry, ProcEntry, EnvEntry.
///
/// Public SDK types registered with IRIS_TYPE so any backend or SDK consumer
/// can use them without including GPL headers.

#pragma once

#include <sdk/cpp/iris.hpp>
#include <cstdint>

struct alignas(8) DirEntry {
    int64_t size;       ///< bytes; 0 for directories
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
