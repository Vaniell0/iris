/// @file src/irish/backend/base_ops/collect.cpp
#include "common.hpp"

namespace iris::irsh::base_ops {

IrType check_collect(const BackendConfig&,
                     const IrType& input,
                     const iris::TypeRegistry&,
                     const Session&,
                     std::vector<TypeError>&,
                     Loc) {
    if (auto* s = std::get_if<StreamType>(&input)) return VecType{s->elem_id};
    if (std::holds_alternative<TextLineType>(input)) return VecType{0};
    return input;
}

IrisGen gen_collect(const BackendConfig&,
                    const iris::TypeDescriptor*,
                    IrisGen upstream,
                    const iris::TypeRegistry&,
                    const Session&) {
    std::vector<iris::IrisValue> buf;
    while (auto _r = upstream()) { if (!*_r) break; buf.push_back(std::move(**_r)); }
    return [buf = std::move(buf), idx = size_t{0}]() mutable -> IrisResult {
        if (idx >= buf.size()) return iris_end();
        return iris_val(std::move(buf[idx++]));
    };
}

} // namespace iris::irsh::base_ops
