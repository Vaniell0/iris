// Builds an ImportTable snapshot from BackendRegistry + Session.imports().
// Include this header in files that instantiate Parser — not in parser itself
// (avoids cyclic CMake dep: irish_parser → irish_backends).
#pragma once
#include "parser.hpp"
#include "../backend/backend_registry.hpp"
#include "../session/session.hpp"
#include <algorithm>

namespace iris::irsh {

inline ImportTable make_import_table(const BackendRegistry& reg, const Session& session) {
    ImportTable t;
    // 'base' is always available — its ops (filter, head, sort, parse, …) are
    // core primitives that should never require an explicit `import @base`.
    auto add_ns = [&](std::string_view ns) {
        auto* b = reg.find(ns);
        if (!b) return;
        for (auto& d : b->ops()) {
            auto it = std::find_if(t.begin(), t.end(), [&](const auto& e) {
                return e.ns == ns && e.op == std::string_view{d.name};
            });
            if (it != t.end()) {
                it->as_source = it->as_source || d.as_source;
                it->as_stage  = it->as_stage  || d.as_stage;
            } else {
                t.push_back({std::string{ns}, std::string{d.name}, d.as_source, d.as_stage, d.config});
            }
        }
    };
    add_ns("base");
    for (const auto& ns : session.imports()) {
        if (ns == "base") continue; // already added above
        add_ns(ns);
    }
    return t;
}

} // namespace iris::irsh
