#pragma once
#include "backend_registry.hpp"
#include <registry.hpp>

namespace iris::irsh {

// Handles @base.filter, @base.sort, @base.select, @base.map,
//         @base.head, @base.collect, @base.print, @base.write,
//         @base.type, @base.types
class BaseIrshBackend : public IrshBackend {
public:
    explicit BaseIrshBackend(const iris::TypeRegistry& global,
                              const iris::TypeRegistry& session)
        : global_(global), session_(session) {}

    std::string_view name() const override { return "base"; }

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
        return {"filter", "sort", "select", "map", "head", "collect",
                "print", "write", "type", "types"};
    }

private:
    const iris::TypeRegistry& global_;
    const iris::TypeRegistry& session_;
};

} // namespace iris::irsh
