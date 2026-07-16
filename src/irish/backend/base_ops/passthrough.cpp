/// @file src/irish/backend/base_ops/passthrough.cpp
/// Shared implementation for `print` and `write` — both are checker
/// no-ops (return Void) and generator passthroughs (executor handles
/// the actual output side-effect from OpDesc).
#include "common.hpp"

namespace iris::irsh::base_ops {

IrType check_passthrough(const BackendConfig&,
                         const IrType&,
                         const iris::TypeRegistry&,
                         const Session&,
                         std::vector<TypeError>&,
                         Loc) {
    return VoidType{};
}

IrisGen gen_passthrough(const BackendConfig&,
                        const iris::TypeDescriptor*,
                        IrisGen upstream,
                        const iris::TypeRegistry&,
                        const Session&) {
    return upstream;
}

} // namespace iris::irsh::base_ops
