/// @file src/irish/backend/base_ops/lit.cpp
#include "common.hpp"
#include "../../exec/eval.hpp"
#include <string>

namespace iris::irsh::base_ops {

IrType check_lit(const BackendConfig&,
                 const IrType&,
                 const iris::TypeRegistry&,
                 const Session&,
                 std::vector<TypeError>&,
                 Loc) {
    return TextLineType{};
}

IrisGen gen_lit(const BackendConfig& config,
                const iris::TypeDescriptor*,
                IrisGen,
                const iris::TypeRegistry&,
                const Session& session) {
    const std::vector<std::string>* args_ptr = &session.script_args();
    std::string val;
    if (auto* s = std::get_if<std::string>(&config)) {
        val = *s;
    } else if (auto* e = std::get_if<Expr>(&config)) {
        if (auto ev = eval_expr(*e, args_ptr)) {
            std::visit([&](const auto& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, std::string>) val = x;
                else val = std::to_string(x);
            }, ev->v);
        }
    }
    bool done = false;
    return [val, done]() mutable -> IrisResult {
        if (done) return iris_end();
        done = true;
        iris::IrisValue v; v.type_id = 0; v.payload = val; return iris_val(std::move(v));
    };
}

} // namespace iris::irsh::base_ops
