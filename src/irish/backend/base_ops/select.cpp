/// @file src/irish/backend/base_ops/select.cpp
#include "common.hpp"
#include "../../checker/checker.hpp"
#include "../../exec/eval.hpp"
#include <registry.hpp>

namespace iris::irsh::base_ops {

IrType check_select(const BackendConfig& config,
                    const IrType& input,
                    const iris::TypeRegistry& global,
                    const Session& session,
                    std::vector<TypeError>& errs,
                    Loc loc) {
    const iris::TypeRegistry* sess = &session.session_types();
    if (auto* e = std::get_if<Expr>(&config)) {
        if (auto* fr = std::get_if<FieldRef>(e)) {
            if (auto* desc = resolve_desc(input, global, sess)) {
                if (auto* f = desc->find_field(fr->name))
                    return ScalarType{f->kind};
                errs.push_back({loc,
                    "select: type '" + desc->name + "' has no field '" + fr->name + "'"});
            }
        }
    }
    return VoidType{};
}

IrisGen gen_select(const BackendConfig& config,
                   const iris::TypeDescriptor* desc,
                   IrisGen upstream,
                   const iris::TypeRegistry&,
                   const Session&) {
    std::string field;
    if (auto* e = std::get_if<Expr>(&config))
        if (auto* fr = std::get_if<FieldRef>(e))
            field = fr->name;
    return [field, desc, up = std::move(upstream)]() mutable -> IrisResult {
        while (auto _r = up()) {
            if (!*_r) return iris_end();
            auto fv = read_field(field, **_r, desc);
            if (!fv) continue;
            std::string s;
            std::visit([&](const auto& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, std::string>)  s = x;
                else if constexpr (std::is_same_v<T, int64_t>) s = std::to_string(x);
                else if constexpr (std::is_same_v<T, double>)  s = std::to_string(x);
                else if constexpr (std::is_same_v<T, bool>)    s = x ? "true" : "false";
            }, fv->v);
            iris::IrisValue out; out.type_id = 0; out.payload = std::move(s);
            return iris_val(std::move(out));
        }
        return iris_end();
    };
}

} // namespace iris::irsh::base_ops
