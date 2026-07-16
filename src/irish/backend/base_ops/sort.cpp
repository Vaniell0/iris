/// @file src/irish/backend/base_ops/sort.cpp
#include "common.hpp"
#include "../../checker/checker.hpp"
#include "../../exec/eval.hpp"
#include <registry.hpp>
#include <algorithm>

namespace iris::irsh::base_ops {

IrType check_sort(const BackendConfig& config,
                  const IrType& input,
                  const iris::TypeRegistry& global,
                  const Session& session,
                  std::vector<TypeError>& errs,
                  Loc loc) {
    const iris::TypeRegistry* sess = &session.session_types();
    if (auto* sa = std::get_if<SortArg>(&config)) {
        if (auto* desc = resolve_desc(input, global, sess))
            if (!desc->find_field(sa->field))
                errs.push_back({loc,
                    "sort: type '" + desc->name + "' has no field '" + sa->field + "'"});
    }
    return input;
}

IrisGen gen_sort(const BackendConfig& config,
                 const iris::TypeDescriptor* desc,
                 IrisGen upstream,
                 const iris::TypeRegistry&,
                 const Session&) {
    std::string field;
    bool desc_order = false;
    if (auto* sa = std::get_if<SortArg>(&config)) {
        field      = sa->field;
        desc_order = sa->desc;
    }
    std::vector<iris::IrisValue> buf;
    while (auto _r = upstream()) { if (!*_r) break; buf.push_back(std::move(**_r)); }

    std::stable_sort(buf.begin(), buf.end(),
        [field, desc_order, d = desc](const iris::IrisValue& a, const iris::IrisValue& b) {
            auto av = read_field(field, a, d);
            auto bv = read_field(field, b, d);
            if (!av || !bv) return false;
            bool less = std::visit([&](const auto& av_val) -> bool {
                using T = std::decay_t<decltype(av_val)>;
                if constexpr (!std::is_same_v<T, bool>)
                    if (auto* bv_val = std::get_if<T>(&bv->v))
                        return av_val < *bv_val;
                return false;
            }, av->v);
            return desc_order ? !less : less;
        });

    return [buf = std::move(buf), idx = size_t{0}]() mutable -> IrisResult {
        if (idx >= buf.size()) return iris_end();
        return iris_val(std::move(buf[idx++]));
    };
}

} // namespace iris::irsh::base_ops
