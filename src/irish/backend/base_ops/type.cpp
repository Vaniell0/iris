/// @file src/irish/backend/base_ops/type.cpp
#include "common.hpp"
#include <registry.hpp>

namespace iris::irsh::base_ops {

IrType check_type(const BackendConfig& config,
                  const IrType&,
                  const iris::TypeRegistry&,
                  const Session&,
                  std::vector<TypeError>& errs,
                  Loc loc) {
    if (!std::get_if<std::string>(&config))
        errs.push_back({loc, "type: expected type name, e.g. type(DirEntry)"});
    return TextLineType{};
}

IrisGen gen_type(const BackendConfig& config,
                 const iris::TypeDescriptor*,
                 IrisGen,
                 const iris::TypeRegistry& global,
                 const Session& session) {
    std::string tname;
    if (auto* s = std::get_if<std::string>(&config)) tname = *s;
    const auto* d = global.find(tname);
    if (!d) d = session.session_types().find(tname);
    std::vector<std::string> lines;
    if (d)
        for (auto& f : d->fields)
            lines.push_back(f.name + ": " + kind_name(f.kind));
    return [lines = std::move(lines), idx = size_t{0}]() mutable -> IrisResult {
        if (idx >= lines.size()) return iris_end();
        iris::IrisValue v; v.type_id = 0; v.payload = lines[idx++];
        return iris_val(std::move(v));
    };
}

} // namespace iris::irsh::base_ops
