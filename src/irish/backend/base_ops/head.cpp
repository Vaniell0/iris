/// @file src/irish/backend/base_ops/head.cpp
#include "common.hpp"
#include "../../exec/eval.hpp"
#include <cstdlib>

namespace iris::irsh::base_ops {

IrType check_head(const BackendConfig&,
                  const IrType& input,
                  const iris::TypeRegistry&,
                  const Session&,
                  std::vector<TypeError>&,
                  Loc) {
    return input;
}

IrisGen gen_head(const BackendConfig& config,
                 const iris::TypeDescriptor*,
                 IrisGen upstream,
                 const iris::TypeRegistry&,
                 const Session& session) {
    const std::vector<std::string>* args_ptr = &session.script_args();
    int64_t limit = 10;
    if (auto* e = std::get_if<Expr>(&config)) {
        if (auto* il = std::get_if<IntLit>(e))
            limit = il->value;
        else if (auto* dv = std::get_if<DollarExpr>(e)) {
            if (auto ev = eval_expr(*e, args_ptr)) {
                if (auto* n = std::get_if<std::string>(&ev->v))
                    limit = std::strtoll(n->c_str(), nullptr, 10);
                else if (auto* n = std::get_if<int64_t>(&ev->v))
                    limit = *n;
            }
            (void)dv;
        }
    }
    return [count = int64_t{0}, limit, up = std::move(upstream)]() mutable -> IrisResult {
        if (count >= limit) return iris_end();
        auto _r = up();
        if (!_r) return _r;      // propagate upstream error
        if (*_r) ++count;
        return _r;
    };
}

} // namespace iris::irsh::base_ops
