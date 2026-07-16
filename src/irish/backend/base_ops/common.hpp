/// @file src/irish/backend/base_ops/common.hpp
/// Shared declarations for @base op implementations.
/// Every op file exposes `check_<op>()` and/or `gen_<op>()` in this
/// namespace; `base_irsh.cpp` is thin dispatch.
#pragma once
#include "../base_irsh.hpp"

namespace iris::irsh::base_ops {

// ── Shared helpers ────────────────────────────────────────────────────────────

const char* kind_name(iris::PrimitiveKind k);

const iris::TypeDescriptor* resolve_desc(const IrType& t,
                                          const iris::TypeRegistry& global,
                                          const iris::TypeRegistry* session);

void walk_expr_fields(const Expr& expr,
                       const iris::TypeDescriptor* desc,
                       bool is_text,
                       std::vector<TypeError>& errors);

// ── Per-op check/gen ──────────────────────────────────────────────────────────
//
// Uniform signatures. `session` provides session-scoped types and script args;
// unused parameters are still passed to keep dispatch flat.

IrType  check_filter(const BackendConfig&, const IrType&, const iris::TypeRegistry&,
                     const Session&, std::vector<TypeError>&, Loc);
IrisGen gen_filter(const BackendConfig&, const iris::TypeDescriptor*, IrisGen upstream,
                   const iris::TypeRegistry&, const Session&);

IrType  check_sort(const BackendConfig&, const IrType&, const iris::TypeRegistry&,
                   const Session&, std::vector<TypeError>&, Loc);
IrisGen gen_sort(const BackendConfig&, const iris::TypeDescriptor*, IrisGen upstream,
                 const iris::TypeRegistry&, const Session&);

IrType  check_select(const BackendConfig&, const IrType&, const iris::TypeRegistry&,
                     const Session&, std::vector<TypeError>&, Loc);
IrisGen gen_select(const BackendConfig&, const iris::TypeDescriptor*, IrisGen upstream,
                   const iris::TypeRegistry&, const Session&);

IrType  check_head(const BackendConfig&, const IrType&, const iris::TypeRegistry&,
                   const Session&, std::vector<TypeError>&, Loc);
IrisGen gen_head(const BackendConfig&, const iris::TypeDescriptor*, IrisGen upstream,
                 const iris::TypeRegistry&, const Session&);

IrType  check_map(const BackendConfig&, const IrType&, const iris::TypeRegistry&,
                  const Session&, std::vector<TypeError>&, Loc);
IrisGen gen_map(const BackendConfig&, const iris::TypeDescriptor*, IrisGen upstream,
                const iris::TypeRegistry&, const Session&);

IrType  check_collect(const BackendConfig&, const IrType&, const iris::TypeRegistry&,
                      const Session&, std::vector<TypeError>&, Loc);
IrisGen gen_collect(const BackendConfig&, const iris::TypeDescriptor*, IrisGen upstream,
                    const iris::TypeRegistry&, const Session&);

IrType  check_lit(const BackendConfig&, const IrType&, const iris::TypeRegistry&,
                  const Session&, std::vector<TypeError>&, Loc);
IrisGen gen_lit(const BackendConfig&, const iris::TypeDescriptor*, IrisGen upstream,
                const iris::TypeRegistry&, const Session&);

IrType  check_types(const BackendConfig&, const IrType&, const iris::TypeRegistry&,
                    const Session&, std::vector<TypeError>&, Loc);
IrisGen gen_types(const BackendConfig&, const iris::TypeDescriptor*, IrisGen upstream,
                  const iris::TypeRegistry&, const Session&);

IrType  check_type(const BackendConfig&, const IrType&, const iris::TypeRegistry&,
                   const Session&, std::vector<TypeError>&, Loc);
IrisGen gen_type(const BackendConfig&, const iris::TypeDescriptor*, IrisGen upstream,
                 const iris::TypeRegistry&, const Session&);

IrType  check_parse(const BackendConfig&, const IrType&, const iris::TypeRegistry&,
                    const Session&, std::vector<TypeError>&, Loc);
IrisGen gen_parse(const BackendConfig&, const iris::TypeDescriptor*, IrisGen upstream,
                  const iris::TypeRegistry&, const Session&);

// print + write share behaviour: check → Void, gen → upstream passthrough.
IrType  check_passthrough(const BackendConfig&, const IrType&, const iris::TypeRegistry&,
                          const Session&, std::vector<TypeError>&, Loc);
IrisGen gen_passthrough(const BackendConfig&, const iris::TypeDescriptor*, IrisGen upstream,
                        const iris::TypeRegistry&, const Session&);

} // namespace iris::irsh::base_ops
