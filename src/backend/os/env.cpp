// SPDX-License-Identifier: Apache-2.0
/// @file   src/backend/os/env.cpp
/// @brief  EnvStream lazy iterator + env() eager convenience wrapper.

#include <backend/os.hpp>
#include <cstring>
#include <cstdlib>

extern char** environ;

namespace iris::os {

// ── EnvStream ─────────────────────────────────────────────────────────────────

std::optional<EnvEntry> EnvStream::next() {
    if (!environ) return std::nullopt;
    while (environ[idx_] != nullptr) {
        const char* ep = environ[idx_++];
        const char* eq = std::strchr(ep, '=');
        if (!eq) continue;
        std::size_t klen = static_cast<std::size_t>(eq - ep);
        EnvEntry e{};
        std::strncpy(e.key, ep, std::min(klen, sizeof(e.key) - 1));
        std::strncpy(e.val, eq + 1, sizeof(e.val) - 1);
        return e;
    }
    return std::nullopt;
}

// ── Eager convenience wrapper ─────────────────────────────────────────────────

std::expected<std::vector<IrisValue>, OsError> env() {
    if (!environ) return std::unexpected(OsError::ReadError);

    std::vector<IrisValue> result;
    for (char** ep = environ; *ep != nullptr; ++ep) {
        const char* eq = std::strchr(*ep, '=');
        if (!eq) continue;
        std::size_t klen = static_cast<std::size_t>(eq - *ep);
        EnvEntry e{};
        std::strncpy(e.key, *ep, std::min(klen, sizeof(e.key) - 1));
        std::strncpy(e.val, eq + 1, sizeof(e.val) - 1);
        result.push_back(iris::wrap(e));
    }
    return result;
}

} // namespace iris::os
