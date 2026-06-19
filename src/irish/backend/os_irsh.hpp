#pragma once
#include "backend_registry.hpp"

namespace iris::irsh {

// Handles @os.ls, @os.ps, @os.env, @os.exec, @os.clear
class OsIrshBackend : public IrshBackend {
public:
    std::string_view name() const override { return "os"; }

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

    std::vector<std::string_view> ops() const override {
        return {"ls", "ps", "env", "exec", "clear"};
    }
};

} // namespace iris::irsh
