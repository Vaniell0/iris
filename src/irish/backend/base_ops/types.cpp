/// @file src/irish/backend/base_ops/types.cpp
#include "common.hpp"
#include <registry.hpp>

namespace iris::irsh::base_ops {

IrType check_types(const BackendConfig&,
                   const IrType&,
                   const iris::TypeRegistry&,
                   const Session&,
                   std::vector<TypeError>&,
                   Loc) {
    return TextLineType{};
}

IrisGen gen_types(const BackendConfig&,
                  const iris::TypeDescriptor*,
                  IrisGen,
                  const iris::TypeRegistry& global,
                  const Session& session) {
    std::vector<std::string> names;
    for (auto& [id, d] : global.all())  names.push_back(d.name);
    for (auto& [id, d] : session.session_types().all()) names.push_back(d.name);
    return [names = std::move(names), idx = size_t{0}]() mutable -> IrisResult {
        if (idx >= names.size()) return iris_end();
        iris::IrisValue v; v.type_id = 0; v.payload = names[idx++];
        return iris_val(std::move(v));
    };
}

} // namespace iris::irsh::base_ops
