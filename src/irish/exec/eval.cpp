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

// ── eval_expr / eval_predicate ───────────────────────────────────────────────

static std::optional<EvalVal> eval_node(
        const Expr& e,
        const iris::IrisValue* val,
        const iris::TypeDescriptor* desc,
        const std::vector<std::string>* args);

static std::optional<EvalVal> eval_node(
        const Expr& e,
        const iris::IrisValue* val,
        const iris::TypeDescriptor* desc,
        const std::vector<std::string>* args) {
    return std::visit([&](const auto& node) -> std::optional<EvalVal> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IntLit>)   return EvalVal{node.value};
        if constexpr (std::is_same_v<T, FloatLit>) return EvalVal{node.value};
        if constexpr (std::is_same_v<T, StrLit>)   return EvalVal{node.value};
        if constexpr (std::is_same_v<T, BoolLit>)  return EvalVal{node.value};
        if constexpr (std::is_same_v<T, FieldRef>) {
            if (!val) return std::nullopt;
            return read_field(node.name, *val, desc);
        }
        if constexpr (std::is_same_v<T, DollarExpr>) {
            if (!args) return std::nullopt;
            if (auto* idx = std::get_if<int64_t>(&node.access)) {
                auto i = static_cast<size_t>(*idx);
                return EvalVal{i < args->size() ? (*args)[i] : std::string{}};
            }
            if (auto* flag = std::get_if<std::string>(&node.access)) {
                for (size_t i = 0; i < args->size(); ++i) {
                    if ((*args)[i] == "--" + *flag) {
                        if (i + 1 < args->size() && (*args)[i+1][0] != '-')
                            return EvalVal{(*args)[i+1]};
                        return EvalVal{true};
                    }
                }
                return EvalVal{false};
            }
            // $args with no subscript — space-joined
            std::string all;
            for (size_t i = 0; i < args->size(); ++i) {
                if (i) all += ' ';
                all += (*args)[i];
            }
            return EvalVal{all};
        }
        if constexpr (std::is_same_v<T, std::shared_ptr<UnOp>>) {
            if (!node) return std::nullopt;
            auto v = eval_node(node->operand, val, desc, args);
            if (!v) return std::nullopt;
            return EvalVal{!v->as_bool()};
        }
        if constexpr (std::is_same_v<T, std::shared_ptr<BinOp>>) {
            if (!node) return std::nullopt;
            if (node->op == BinOpKind::And) {
                auto l = eval_node(node->lhs, val, desc, args);
                if (!l || !l->as_bool()) return EvalVal{false};
                auto r = eval_node(node->rhs, val, desc, args);
                return r ? EvalVal{r->as_bool()} : EvalVal{false};
            }
            if (node->op == BinOpKind::Or) {
                auto l = eval_node(node->lhs, val, desc, args);
                if (l && l->as_bool()) return EvalVal{true};
                auto r = eval_node(node->rhs, val, desc, args);
                return r ? EvalVal{r->as_bool()} : EvalVal{false};
            }
            auto l = eval_node(node->lhs, val, desc, args);
            auto r = eval_node(node->rhs, val, desc, args);
            if (!l || !r) return std::nullopt;
            return EvalVal{compare(*l, *r, node->op)};
        }
        return std::nullopt;
    }, e);
}

std::optional<EvalVal> eval_expr(const Expr& expr,
                                  const std::vector<std::string>* args) {
    return eval_node(expr, nullptr, nullptr, args);
}

bool eval_predicate(const Expr& expr,
                    const iris::IrisValue& val,
                    const iris::TypeDescriptor* desc,
                    const std::vector<std::string>* args) {
    auto v = eval_node(expr, &val, desc, args);
    return v && v->as_bool();
}

} // namespace iris::irsh
