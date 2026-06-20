#pragma once
#include "backend_registry.hpp"

namespace iris::irsh {

// Handles @ipc("./socket") — serialize stream over Unix socket.
// Wire format: [type_id:u64][size:u32][payload:size bytes]
// Only wire-safe types (no Str/Bytes/CStr fields) accepted — enforced by Checker.
class IpcIrshBackend : public IrshBackend {
public:
    std::string_view name() const override { return "ipc"; }

    IrType check(std::string_view op,
                 const BackendConfig& config,
                 const IrType& input,
                 const TypeRegistry& global,
                 std::vector<TypeError>& errs,
                 Loc loc) const override;

    IrisGen make_gen(std::string_view op,
                     const BackendConfig& config,
                     const TypeDescriptor* desc,
                     IrisGen upstream) override;

    // No bare-word ops — always used as @ipc("./path"), not as import shorthand.
    std::vector<OpDesc> ops() const override { return {}; }
};

} // namespace iris::irsh
