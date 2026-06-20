// SPDX-License-Identifier: Apache-2.0
/// @file   src/backend/os/ls.cpp
/// @brief  LsStream lazy iterator + ls() eager convenience wrapper.

#include <backend/os.hpp>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace iris::os {

// ── LsStream ──────────────────────────────────────────────────────────────────

bool LsStream::open() {
    dir_ = opendir(path_.c_str());
    return dir_ != nullptr;
}

void LsStream::close() {
    if (dir_) { closedir(dir_); dir_ = nullptr; }
}

std::optional<DirEntry> LsStream::next() {
    struct dirent* ent;
    while ((ent = readdir(dir_)) != nullptr) {
        if (!show_hidden_ && ent->d_name[0] == '.') continue;

        std::string full = path_ + "/" + ent->d_name;
        struct stat st{};
        if (lstat(full.c_str(), &st) != 0) continue;

        DirEntry e{};
        e.size  = S_ISREG(st.st_mode) ? static_cast<int64_t>(st.st_size) : 0;
        e.mtime = static_cast<int64_t>(st.st_mtime);
        e.mode  = static_cast<int32_t>(st.st_mode & 0777);
        if      (S_ISREG(st.st_mode)) e.type = 0;
        else if (S_ISDIR(st.st_mode)) e.type = 1;
        else if (S_ISLNK(st.st_mode)) e.type = 2;
        else                           e.type = 3;
        std::snprintf(e.name, sizeof(e.name), "%s", ent->d_name);
        return e;
    }
    return std::nullopt;
}

// ── Eager convenience wrapper ─────────────────────────────────────────────────

std::expected<std::vector<IrisValue>, OsError> ls(std::string_view path) {
    DIR* dir = opendir(std::string(path).c_str());
    if (!dir) {
        return std::unexpected(errno == EACCES ? OsError::PermissionDenied
                                               : OsError::NotFound);
    }

    std::vector<IrisValue> result;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;

        std::string full_path = std::string(path) + "/" + ent->d_name;
        struct stat st{};
        if (lstat(full_path.c_str(), &st) != 0) continue;

        DirEntry e{};
        e.size  = S_ISREG(st.st_mode) ? static_cast<int64_t>(st.st_size) : 0;
        e.mtime = static_cast<int64_t>(st.st_mtime);
        e.mode  = static_cast<int32_t>(st.st_mode & 0777);
        if      (S_ISREG(st.st_mode)) e.type = 0;
        else if (S_ISDIR(st.st_mode)) e.type = 1;
        else if (S_ISLNK(st.st_mode)) e.type = 2;
        else                           e.type = 3;
        std::snprintf(e.name, sizeof(e.name), "%s", ent->d_name);
        result.push_back(iris::wrap(e));
    }

    closedir(dir);
    return result;
}

} // namespace iris::os
