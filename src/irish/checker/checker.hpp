#pragma once
#include "irtype.hpp"
#include "../parser/ast.hpp"
#include "../backend/backend_registry.hpp"
#include <registry.hpp>
#include <string>
#include <vector>
#include <variant>

namespace iris::irsh {

struct TypeError {
    Loc         loc;
    std::string msg;
};

// Annotated AST node — every stage gets its output type after checking.
struct TypedStage {
    Stage  stage;    // Stage = BackendCall
    IrType out_type;
};

struct TypedPipeline {
    BackendCall             source;
    IrType                  source_type;
    std::vector<TypedStage> stages;
};

// ── Typed statement variants ──────────────────────────────────────────────────

struct TypedLetStmt  { std::string name; TypedPipeline rhs; Loc loc; };
struct TypedExprStmt { TypedPipeline pipeline; };

using TypedStatement = std::variant<TypedLetStmt, TypedExprStmt>;

struct TypedProgram {
    std::vector<TypedStatement> stmts;
    std::vector<TypeError>      errors;
    bool ok() const { return errors.empty(); }
};

// ── Checker ───────────────────────────────────────────────────────────────────

class Checker {
public:
    // global   — TypeRegistry::global(), frozen
    // session  — TypeRegistry::session(), live
    // registry — BackendRegistry with all registered backends
    Checker(const TypeRegistry& global, TypeRegistry& session, BackendRegistry& registry);

    TypedProgram check(const Program& program);

    // Public for session variable pipeline composition in main.cpp.
    IrType check_stage(const Stage& stage, const IrType& input);

private:
    const TypeRegistry& global_;
    TypeRegistry&       session_;
    BackendRegistry&    registry_;

    TypedPipeline check_pipeline(const Pipeline& p);
    IrType        check_source(const BackendCall& ref);

    const TypeDescriptor* resolve_elem_type(const IrType&) const;
    void emit_error(Loc, std::string msg);

    std::vector<TypeError> errors_;
};

} // namespace iris::irsh
