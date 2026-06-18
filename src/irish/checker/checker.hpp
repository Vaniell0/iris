#pragma once
#include "irtype.hpp"
#include "../parser/ast.hpp"
#include <registry.hpp>
#include <string>
#include <vector>

namespace iris::irsh {

struct TypeError {
    Loc         loc;
    std::string msg;
};

// Annotated AST node — every stage gets its output type after checking.
struct TypedStage {
    Stage  stage;
    IrType out_type;
};

struct TypedPipeline {
    BackendRef             source;
    IrType                 source_type;
    std::vector<TypedStage> stages;
};

struct TypedProgram {
    // TODO: typed statement variants
    std::vector<TypeError> errors;
    bool ok() const { return errors.empty(); }
};

class Checker {
public:
    // global  — TypeRegistry::global(), frozen
    // session — TypeRegistry::session(), live
    Checker(const TypeRegistry& global, TypeRegistry& session);

    TypedProgram check(const Program& program);

private:
    const TypeRegistry& global_;
    TypeRegistry&       session_;

    IrType check_source(const BackendRef& ref);
    IrType check_stage(const Stage& stage, const IrType& input);
    IrType check_filter(const FilterStage&, const IrType& input);
    IrType check_sort(const SortStage&,   const IrType& input);
    IrType check_map(const MapStage&,     const IrType& input);
    IrType check_select(const SelectStage&, const IrType& input);

    // Resolve @ns.op(config) → backend + verify wire-safety when needed
    IrType check_backend_stage(const BackendStage&, const IrType& input);

    const TypeDescriptor* resolve_elem_type(const IrType&) const;
    void emit_error(Loc, std::string msg);

    std::vector<TypeError> errors_;
};

} // namespace iris::irsh
