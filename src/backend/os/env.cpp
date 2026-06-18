// SPDX-License-Identifier: Apache-2.0
/// @file   src/backend/os/env.cpp
/// @brief  env — expose process environment as EnvEntry IrisValues.

#include <backend/os.hpp>
#include <cstring>
#include <cstdlib>

extern char** environ;

namespace iris::os {

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
