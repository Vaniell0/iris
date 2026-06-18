// SPDX-License-Identifier: Apache-2.0
/// @file   src/backend/os/ps.cpp
/// @brief  PsStream lazy iterator + ps() eager convenience wrapper.

#include <backend/os.hpp>
#include <dirent.h>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <fstream>
#include <string>

namespace iris::os {

static bool all_digits(const char* s) {
    if (!*s) return false;
    for (; *s; ++s) if (!std::isdigit(static_cast<unsigned char>(*s))) return false;
    return true;
}

static bool parse_status(const char* pid_str, ProcEntry& e) {
    std::string status_path = std::string("/proc/") + pid_str + "/status";
    std::ifstream f(status_path);
    if (!f.is_open()) return false;

    std::string line;
    try {
        while (std::getline(f, line)) {
            if (line.rfind("Name:\t", 0) == 0) {
                std::strncpy(e.name, line.c_str() + 6, sizeof(e.name) - 1);
                int len = static_cast<int>(std::strlen(e.name));
                while (len > 0 && (e.name[len-1] == '\n' || e.name[len-1] == '\r'
                                   || e.name[len-1] == ' ' || e.name[len-1] == '\t'))
                    e.name[--len] = '\0';
            } else if (line.rfind("State:\t", 0) == 0) { e.state[0] = line[7];
            } else if (line.rfind("Pid:\t",  0) == 0)  { e.pid  = std::stoi(line.substr(5));
            } else if (line.rfind("PPid:\t", 0) == 0)  { e.ppid = std::stoi(line.substr(6));
            } else if (line.rfind("VmRSS:\t", 0) == 0) { e.rss  = std::stoll(line.substr(7));
            }
        }
    } catch (...) { return false; }
    return e.pid > 0;
}

// ── PsStream ──────────────────────────────────────────────────────────────────

bool PsStream::open() {
    proc_ = opendir("/proc");
    return proc_ != nullptr;
}

void PsStream::close() {
    if (proc_) { closedir(proc_); proc_ = nullptr; }
}

std::optional<ProcEntry> PsStream::next() {
    struct dirent* ent;
    while ((ent = readdir(proc_)) != nullptr) {
        if (!all_digits(ent->d_name)) continue;
        ProcEntry e{};
        if (parse_status(ent->d_name, e)) return e;
    }
    return std::nullopt;
}

// ── Eager convenience wrapper ─────────────────────────────────────────────────

std::expected<std::vector<IrisValue>, OsError> ps() {
    DIR* proc = opendir("/proc");
    if (!proc) return std::unexpected(OsError::ReadError);

    std::vector<IrisValue> result;
    struct dirent* ent;
    while ((ent = readdir(proc)) != nullptr) {
        if (!all_digits(ent->d_name)) continue;
        ProcEntry e{};
        if (parse_status(ent->d_name, e)) result.push_back(iris::wrap(e));
    }

    closedir(proc);
    return result;
}

} // namespace iris::os
