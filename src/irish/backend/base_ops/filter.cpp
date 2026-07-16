/// @file src/irish/backend/base_ops/filter.cpp
#include "common.hpp"
#include "../../checker/checker.hpp"
#include "../../exec/eval.hpp"
#include <registry.hpp>

namespace iris::irsh::base_ops {

IrType check_filter(const BackendConfig& config,
                    const IrType& input,
                    const iris::TypeRegistry& global,
                    const Session& session,
                    std::vector<TypeError>& errs,
                    Loc loc) {
    const iris::TypeRegistry* sess = &session.session_types();
    bool is_text = std::holds_alternative<TextLineType>(input);
    const iris::TypeDescriptor* desc = resolve_desc(input, global, sess);
    if (!is_text && !desc)
        errs.push_back({loc, "filter: input is not a stream"});
    else if (auto* pred = std::get_if<Expr>(&config))
        walk_expr_fields(*pred, desc, is_text, errs);
    return input;
}

IrisGen gen_filter(const BackendConfig& config,
                   const iris::TypeDescriptor* desc,
                   IrisGen upstream,
                   const iris::TypeRegistry&,
                   const Session& session) {
    const std::vector<std::string>* args_ptr = &session.script_args();
    Expr pred = BoolLit{true, {}};
    if (auto* e = std::get_if<Expr>(&config)) pred = *e;
    return [pred, desc, args_ptr, up = std::move(upstream)]() mutable -> IrisResult {
        while (auto _r = up()) {
            if (!*_r) return iris_end();
            if (eval_predicate(pred, **_r, desc, args_ptr)) return iris_val(std::move(**_r));
        }
        return iris_end();
    };
}

} // namespace iris::irsh::base_ops
