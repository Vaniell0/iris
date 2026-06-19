/// @file src/irish/exec/eval.cpp
#include "eval.hpp"
#include <cstring>

namespace iris::irsh {

// ── scalar comparison ─────────────────────────────────────────────────────────

static bool compare(const EvalVal& l, const EvalVal& r, BinOpKind op) {
    return std::visit([&](const auto& lv) -> bool {
        using LT = std::decay_t<decltype(lv)>;
        if (auto* rv = std::get_if<LT>(&r.v)) {
            switch (op) {
                case BinOpKind::Eq: return lv == *rv;
                case BinOpKind::Ne: return lv != *rv;
                case BinOpKind::Lt:
                    if constexpr (!std::is_same_v<LT, bool>) return lv < *rv;
                    break;
                case BinOpKind::Le:
                    if constexpr (!std::is_same_v<LT, bool>) return lv <= *rv;
                    break;
                case BinOpKind::Gt:
                    if constexpr (!std::is_same_v<LT, bool>) return lv > *rv;
                    break;
                case BinOpKind::Ge:
                    if constexpr (!std::is_same_v<LT, bool>) return lv >= *rv;
                    break;
                default: break;
            }
        }
        // int64 vs double coercion
        if constexpr (std::is_same_v<LT, int64_t>) {
            if (auto* rv = std::get_if<double>(&r.v)) {
                double lf = static_cast<double>(lv);
                switch (op) {
                    case BinOpKind::Eq: return lf == *rv;
                    case BinOpKind::Ne: return lf != *rv;
                    case BinOpKind::Lt: return lf <  *rv;
                    case BinOpKind::Le: return lf <= *rv;
                    case BinOpKind::Gt: return lf >  *rv;
                    case BinOpKind::Ge: return lf >= *rv;
                    default: break;
                }
            }
        }
        // String predicates
        if constexpr (std::is_same_v<LT, std::string>) {
            if (auto* rv = std::get_if<std::string>(&r.v)) {
                switch (op) {
                    case BinOpKind::Contains:   return lv.find(*rv) != std::string::npos;
                    case BinOpKind::StartsWith: return lv.starts_with(*rv);
                    case BinOpKind::EndsWith:   return lv.ends_with(*rv);
                    case BinOpKind::Matches:    return lv.find(*rv) != std::string::npos; // regex TODO
                    default: break;
                }
            }
        }
        return false;
    }, l.v);
}

// ── read_field ────────────────────────────────────────────────────────────────

std::optional<EvalVal> read_field(std::string_view field,
                                   const iris::IrisValue& val,
                                   const iris::TypeDescriptor* desc) {
    if (!desc) {
        if (!val.is_str()) return std::nullopt;
        if (field == "text" || field == "line")
            return EvalVal{std::get<std::string>(val.payload)};
        return std::nullopt;
    }

    if (!val.is_raw()) return std::nullopt;
    const auto* fd = desc->find_field(field);
    if (!fd) return std::nullopt;

    const auto* ptr = reinterpret_cast<const char*>(val.raw().data()) + fd->offset;
    using K = iris::PrimitiveKind;
    switch (fd->kind) {
        case K::Bool:  { bool    b; std::memcpy(&b, ptr, 1); return EvalVal{b}; }
        case K::I8:    { int8_t  n; std::memcpy(&n, ptr, 1); return EvalVal{(int64_t)n}; }
        case K::I16:   { int16_t n; std::memcpy(&n, ptr, 2); return EvalVal{(int64_t)n}; }
        case K::I32:   { int32_t n; std::memcpy(&n, ptr, 4); return EvalVal{(int64_t)n}; }
        case K::I64:   { int64_t n; std::memcpy(&n, ptr, 8); return EvalVal{n}; }
        case K::F32:   { float   f; std::memcpy(&f, ptr, 4); return EvalVal{(double)f}; }
        case K::F64:   { double  d; std::memcpy(&d, ptr, 8); return EvalVal{d}; }
        case K::CStr:
        case K::Str: {
            size_t len = ::strnlen(ptr, fd->size);
            return EvalVal{std::string{ptr, len}};
        }
        default: return std::nullopt;
    }
}

// ── eval_predicate ────────────────────────────────────────────────────────────

bool eval_predicate(const Expr& expr,
                    const iris::IrisValue& val,
                    const iris::TypeDescriptor* desc) {
    auto eval = [&](auto& self, const Expr& e) -> std::optional<EvalVal> {
        return std::visit([&](const auto& node) -> std::optional<EvalVal> {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, IntLit>)   return EvalVal{node.value};
            if constexpr (std::is_same_v<T, FloatLit>) return EvalVal{node.value};
            if constexpr (std::is_same_v<T, StrLit>)   return EvalVal{node.value};
            if constexpr (std::is_same_v<T, BoolLit>)  return EvalVal{node.value};
            if constexpr (std::is_same_v<T, FieldRef>) return read_field(node.name, val, desc);
            if constexpr (std::is_same_v<T, std::shared_ptr<UnOp>>) {
                if (!node) return std::nullopt;
                auto v = self(self, node->operand);
                if (!v) return std::nullopt;
                return EvalVal{!v->as_bool()};
            }
            if constexpr (std::is_same_v<T, std::shared_ptr<BinOp>>) {
                if (!node) return std::nullopt;
                if (node->op == BinOpKind::And) {
                    auto l = self(self, node->lhs);
                    if (!l || !l->as_bool()) return EvalVal{false};
                    auto r = self(self, node->rhs);
                    return r ? EvalVal{r->as_bool()} : EvalVal{false};
                }
                if (node->op == BinOpKind::Or) {
                    auto l = self(self, node->lhs);
                    if (l && l->as_bool()) return EvalVal{true};
                    auto r = self(self, node->rhs);
                    return r ? EvalVal{r->as_bool()} : EvalVal{false};
                }
                auto l = self(self, node->lhs);
                auto r = self(self, node->rhs);
                if (!l || !r) return std::nullopt;
                return EvalVal{compare(*l, *r, node->op)};
            }
            return std::nullopt;
        }, e);
    };
    auto v = eval(eval, expr);
    return v && v->as_bool();
}

} // namespace iris::irsh
