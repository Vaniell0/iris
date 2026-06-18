/// @file src/irish/checker/checker.cpp
#include "checker.hpp"

namespace iris::irsh {

Checker::Checker(const TypeRegistry& global, TypeRegistry& session)
    : global_(global), session_(session) {}

// ── TODO: implement ───────────────────────────────────────────────────────────

TypedProgram Checker::check(const Program&)                              { return {}; }
IrType       Checker::check_source(const BackendRef&)                   { return VoidType{}; }
IrType       Checker::check_stage(const Stage&, const IrType&)          { return VoidType{}; }
IrType       Checker::check_filter(const FilterStage&, const IrType& i) { return i; }
IrType       Checker::check_sort(const SortStage&, const IrType& i)     { return i; }
IrType       Checker::check_map(const MapStage&, const IrType&)         { return VoidType{}; }
IrType       Checker::check_select(const SelectStage&, const IrType&)   { return VoidType{}; }
IrType       Checker::check_backend_stage(const BackendStage&, const IrType& i) { return i; }

const TypeDescriptor* Checker::resolve_elem_type(const IrType& t) const {
    if (auto* s = std::get_if<StreamType>(&t))
        return global_.find(s->elem_id);
    return nullptr;
}

void Checker::emit_error(Loc loc, std::string msg) {
    errors_.push_back({loc, std::move(msg)});
}

} // namespace iris::irsh
