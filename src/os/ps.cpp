// SPDX-License-Identifier: Apache-2.0
/// @file   src/os/ps.cpp
/// @brief  ps — read /proc and emit ProcEntry IrisValues.

#include <os.hpp>
#include <dirent.h>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <fstream>
#include <string>

namespace iris::os {

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Returns true if every character in @p s is an ASCII digit (rejects empty).
static bool all_digits(const char* s) {
    if (!*s) return false;
    for (; *s; ++s)
        if (!std::isdigit(static_cast<unsigned char>(*s))) return false;
    return true;
}

// ── Implementation ───────────────────────────────────────────────────────────

std::expected<std::vector<IrisValue>, OsError> ps() {
    DIR* proc = opendir("/proc");
    if (!proc) return std::unexpected(OsError::ReadError);

    std::vector<IrisValue> result;
    struct dirent* ent;
    while ((ent = readdir(proc)) != nullptr) {
        if (!all_digits(ent->d_name)) continue;

        std::string status_path = std::string("/proc/") + ent->d_name + "/status";
        std::ifstream f(status_path);
        if (!f.is_open()) continue;

        ProcEntry e{};
        std::string line;
        try {
            while (std::getline(f, line)) {
                if (line.rfind("Name:\t", 0) == 0) {
                    std::strncpy(e.name, line.c_str() + 6, sizeof(e.name) - 1);
                    int len = static_cast<int>(std::strlen(e.name));
                    while (len > 0 && (e.name[len-1] == '\n' || e.name[len-1] == '\r'
                                       || e.name[len-1] == ' ' || e.name[len-1] == '\t'))
                        e.name[--len] = '\0';
                } else if (line.rfind("State:\t", 0) == 0) {
                    e.state[0] = line[7];
                } else if (line.rfind("Pid:\t",  0) == 0) {
                    e.pid  = std::stoi(line.substr(5));
                } else if (line.rfind("PPid:\t", 0) == 0) {
                    e.ppid = std::stoi(line.substr(6));
                } else if (line.rfind("VmRSS:\t", 0) == 0) {
                    e.rss  = std::stoll(line.substr(7));
                }
            }
        } catch (...) { continue; }

        if (e.pid > 0) result.push_back(iris::wrap(e));
    }

    closedir(proc);
    return result;
}

} // namespace iris::os
