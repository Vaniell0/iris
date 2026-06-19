/// @file src/irish/checker/checker.cpp
#include "checker.hpp"

namespace iris::irsh {

Checker::Checker(const TypeRegistry& global, TypeRegistry& session, BackendRegistry& registry)
    : global_(global), session_(session), registry_(registry) {}

void Checker::emit_error(Loc loc, std::string msg) {
    errors_.push_back({loc, std::move(msg)});
}

const TypeDescriptor* Checker::resolve_elem_type(const IrType& t) const {
    if (auto* s = std::get_if<StreamType>(&t))
        return global_.find(s->elem_id);
    return nullptr;
}

// ── source ────────────────────────────────────────────────────────────────────

IrType Checker::check_source(const BackendCall& ref) {
    if (ref.ns == "_var") return AnyType{};  // session variable; actual type resolved at runtime
    auto* b = registry_.find(ref.ns);
    if (!b) {
        emit_error(ref.loc, "@" + ref.ns + ": unknown backend");
        return VoidType{};
    }
    return b->check(ref.op, ref.config, VoidType{}, global_, errors_, ref.loc);
}

// ── stage dispatch ────────────────────────────────────────────────────────────

IrType Checker::check_stage(const Stage& stage, const IrType& input) {
    auto* b = registry_.find(stage.ns);
    if (!b) {
        emit_error(stage.loc, "@" + stage.ns + ": unknown backend");
        return VoidType{};
    }
    return b->check(stage.op, stage.config, input, global_, errors_, stage.loc);
}

// ── pipeline ──────────────────────────────────────────────────────────────────

TypedPipeline Checker::check_pipeline(const Pipeline& p) {
    TypedPipeline tp;
    tp.source      = p.source;
    tp.source_type = check_source(p.source);
    IrType cur     = tp.source_type;
    for (auto& stage : p.stages) {
        IrType out = check_stage(stage, cur);
        tp.stages.push_back({stage, out});
        cur = out;
    }
    return tp;
}

// ── TypeDecl kind parsing ─────────────────────────────────────────────────────

static PrimitiveKind parse_kind(std::string_view s) {
    using K = PrimitiveKind;
    if (s == "bool"  || s == "Bool")  return K::Bool;
    if (s == "i8"    || s == "I8")    return K::I8;
    if (s == "i16"   || s == "I16")   return K::I16;
    if (s == "i32"   || s == "I32")   return K::I32;
    if (s == "i64"   || s == "I64")   return K::I64;
    if (s == "f32"   || s == "F32")   return K::F32;
    if (s == "f64"   || s == "F64")   return K::F64;
    if (s == "str"   || s == "Str")   return K::Str;
    if (s == "cstr"  || s == "CStr")  return K::CStr;
    if (s == "bytes" || s == "Bytes") return K::Bytes;
    return K::Void;
}

// ── program ───────────────────────────────────────────────────────────────────

TypedProgram Checker::check(const Program& program) {
    TypedProgram result;
    for (auto& stmt : program.stmts) {
        std::visit([&](const auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, Pipeline>) {
                result.stmts.push_back(TypedExprStmt{check_pipeline(s)});
            } else if constexpr (std::is_same_v<T, LetStmt>) {
                TypedPipeline tp = check_pipeline(s.rhs);
                result.stmts.push_back(TypedLetStmt{s.name, std::move(tp), s.loc});
            } else if constexpr (std::is_same_v<T, TypeDecl>) {
                std::vector<FieldDesc> fds;
                size_t off = 0;
                for (auto& f : s.fields) {
                    PrimitiveKind k = parse_kind(f.kind);
                    size_t sz = f.size ? f.size : 8;
                    fds.push_back({f.name, k, off, sz, ""});
                    off += sz;
                }
                (void)session_.from_fields(s.name, std::move(fds), off);
            }
            // ParallelStmt: not yet supported — skipped silently
        }, stmt);
    }
    result.errors = std::move(errors_);
    return result;
}

} // namespace iris::irsh
