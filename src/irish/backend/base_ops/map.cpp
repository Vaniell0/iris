/// @file src/irish/backend/base_ops/map.cpp
#include "common.hpp"
#include "../../checker/checker.hpp"
#include "../../exec/eval.hpp"
#include <registry.hpp>

namespace iris::irsh::base_ops {

IrType check_map(const BackendConfig& config,
                 const IrType& input,
                 const iris::TypeRegistry& global,
                 const Session& session,
                 std::vector<TypeError>& errs,
                 Loc loc) {
    const iris::TypeRegistry* sess = &session.session_types();
    if (auto* fields = std::get_if<std::vector<std::string>>(&config))
        if (auto* desc = resolve_desc(input, global, sess))
            for (auto& f : *fields)
                if (!desc->find_field(f))
                    errs.push_back({loc,
                        "map: type '" + desc->name + "' has no field '" + f + "'"});
    return TextLineType{};
}

IrisGen gen_map(const BackendConfig& config,
                const iris::TypeDescriptor* desc,
                IrisGen upstream,
                const iris::TypeRegistry&,
                const Session&) {
    // Project selected fields to tab-separated text lines.
    // Typed struct projection (map returning a new struct type) requires
    // session-level TypeDescriptor registration — deferred to Part 2.x.
    auto fields = std::get_if<std::vector<std::string>>(&config);
    if (!fields || fields->empty()) return upstream;
    auto fnames = *fields;
    return [fnames, desc, up = std::move(upstream)]() mutable -> IrisResult {
        while (auto _r = up()) {
            if (!*_r) return iris_end();
            std::string line;
            for (size_t i = 0; i < fnames.size(); ++i) {
                auto fv = read_field(fnames[i], **_r, desc);
                if (i) line += '\t';
                if (fv) std::visit([&](const auto& x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, std::string>) line += x;
                    else line += std::to_string(x);
                }, fv->v);
            }
            iris::IrisValue out; out.type_id = 0; out.payload = std::move(line);
            return iris_val(std::move(out));
        }
        return iris_end();
    };
}

} // namespace iris::irsh::base_ops
