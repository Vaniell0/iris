#pragma once
#include "backend_registry.hpp"
#include "../session/session.hpp"
#include <registry.hpp>

namespace iris::irsh {

// Handles @base.filter, @base.sort, @base.select, @base.map,
//         @base.head, @base.collect, @base.print, @base.write,
//         @base.type, @base.types, @base.parse
class BaseIrshBackend : public IrshBackend {
public:
    explicit BaseIrshBackend(const iris::TypeRegistry& global,
                              const Session& session)
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

    std::vector<OpDesc> ops() const override {
        using CK = ConfigKind;
        return {
            {"types",   true,  true,  CK::None     },
            {"lit",     true,  false, CK::Lit      },
            {"filter",  false, true,  CK::Expr     },
            {"sort",    false, true,  CK::SortArg  },
            {"select",  false, true,  CK::Expr     },
            {"map",     false, true,  CK::FieldList},
            {"head",    false, true,  CK::IntExpr  },
            {"collect", false, true,  CK::None     },
            {"print",   false, true,  CK::None     },
            {"write",   false, true,  CK::String   },
            {"type",    false, true,  CK::TypeName },
            {"parse",   false, true,  CK::TypeName },
        };
    }

private:
    const iris::TypeRegistry& global_;
    const Session&            session_;
};

} // namespace iris::irsh
