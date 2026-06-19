/// @file src/irish/backend/base_irsh.cpp
#include "base_irsh.hpp"
#include "../checker/checker.hpp"
#include "../exec/eval.hpp"
#include <registry.hpp>
#include <algorithm>
#include <memory>

namespace {
static const char* kind_name(iris::PrimitiveKind k) {
    using K = iris::PrimitiveKind;
    switch (k) {
        case K::Void:  return "void";
        case K::Bool:  return "bool";
        case K::I8:    return "i8";   case K::I16:  return "i16";
        case K::I32:   return "i32";  case K::I64:  return "i64";
        case K::F32:   return "f32";  case K::F64:  return "f64";
        case K::Str:   return "str";  case K::CStr: return "cstr";
        case K::Bytes: return "bytes";
    }
    return "?";
}
} // anonymous namespace

namespace iris::irsh {

// ── helpers ───────────────────────────────────────────────────────────────────

static const TypeDescriptor* resolve_desc(const IrType& t,
                                           const TypeRegistry& global) {
    if (auto* s = std::get_if<StreamType>(&t))
        return global.find(s->elem_id);
    return nullptr;
}

static void walk_expr_fields(const Expr& expr, const TypeDescriptor* desc,
                              bool is_text, std::vector<TypeError>& errors) {
    std::visit([&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, FieldRef>) {
            if (is_text) {
                if (node.name != "text" && node.name != "line")
                    errors.push_back({node.loc,
                        "text stream has no field '" + node.name + "' (use 'text' or 'line')"});
            } else if (desc) {
                if (!desc->find_field(node.name))
                    errors.push_back({node.loc,
                        "type '" + desc->name + "' has no field '" + node.name + "'"});
            }
        } else if constexpr (std::is_same_v<T, std::shared_ptr<BinOp>>) {
            if (node) {
                walk_expr_fields(node->lhs, desc, is_text, errors);
                walk_expr_fields(node->rhs, desc, is_text, errors);
            }
        } else if constexpr (std::is_same_v<T, std::shared_ptr<UnOp>>) {
            if (node) walk_expr_fields(node->operand, desc, is_text, errors);
        }
    }, expr);
}

// ── check ─────────────────────────────────────────────────────────────────────

IrType BaseIrshBackend::check(std::string_view op,
                               const BackendConfig& config,
                               const IrType& input,
                               const TypeRegistry& global,
                               std::vector<TypeError>& errs,
                               Loc loc) const {
    if (op == "filter") {
        bool is_text = std::holds_alternative<TextLineType>(input);
        const TypeDescriptor* desc = resolve_desc(input, global);
        if (!is_text && !desc)
            errs.push_back({loc, "filter: input is not a stream"});
        else if (auto* pred = std::get_if<Expr>(&config))
            walk_expr_fields(*pred, desc, is_text, errs);
        return input;
    }
    if (op == "sort") {
        if (auto* sa = std::get_if<SortArg>(&config)) {
            if (auto* desc = resolve_desc(input, global))
                if (!desc->find_field(sa->field))
                    errs.push_back({loc,
                        "sort: type '" + desc->name + "' has no field '" + sa->field + "'"});
        }
        return input;
    }
    if (op == "select") {
        if (auto* e = std::get_if<Expr>(&config)) {
            if (auto* fr = std::get_if<FieldRef>(e)) {
                if (auto* desc = resolve_desc(input, global)) {
                    if (auto* f = desc->find_field(fr->name))
                        return ScalarType{f->kind};
                    errs.push_back({loc,
                        "select: type '" + desc->name + "' has no field '" + fr->name + "'"});
                }
            }
        }
        return VoidType{};
    }
    if (op == "head" || op == "map") return input;
    if (op == "collect") {
        if (auto* s = std::get_if<StreamType>(&input)) return VecType{s->elem_id};
        if (std::holds_alternative<TextLineType>(input)) return VecType{0};
        return input;
    }
    if (op == "print" || op == "write") return VoidType{};
    if (op == "types") return TextLineType{};
    if (op == "type")  return TextLineType{};
    errs.push_back({loc, "@base." + std::string{op} + ": unknown operation"});
    return VoidType{};
}

// ── make_gen ──────────────────────────────────────────────────────────────────

IrisGen BaseIrshBackend::make_gen(std::string_view op,
                                   const BackendConfig& config,
                                   const TypeDescriptor* desc,
                                   IrisGen upstream) {
    if (op == "filter") {
        Expr pred = BoolLit{true, {}};
        if (auto* e = std::get_if<Expr>(&config)) pred = *e;
        return [pred, desc, up = std::move(upstream)]() mutable
               -> std::optional<iris::IrisValue> {
            while (auto v = up()) {
                if (eval_predicate(pred, *v, desc)) return v;
            }
            return std::nullopt;
        };
    }
    if (op == "head") {
        int64_t limit = 10;
        if (auto* e = std::get_if<Expr>(&config))
            if (auto* il = std::get_if<IntLit>(e))
                limit = il->value;
        return [count = int64_t{0}, limit, up = std::move(upstream)]() mutable
               -> std::optional<iris::IrisValue> {
            if (count >= limit) return std::nullopt;
            auto v = up();
            if (v) ++count;
            return v;
        };
    }
    if (op == "sort") {
        std::string field;
        bool desc_order = false;
        if (auto* sa = std::get_if<SortArg>(&config)) {
            field      = sa->field;
            desc_order = sa->desc;
        }
        std::vector<iris::IrisValue> buf;
        while (auto v = upstream()) buf.push_back(std::move(*v));

        std::stable_sort(buf.begin(), buf.end(),
            [field, desc_order, d = desc](const iris::IrisValue& a, const iris::IrisValue& b) {
                auto av = read_field(field, a, d);
                auto bv = read_field(field, b, d);
                if (!av || !bv) return false;
                bool less = std::visit([&](const auto& av_val) -> bool {
                    using T = std::decay_t<decltype(av_val)>;
                    if constexpr (!std::is_same_v<T, bool>)
                        if (auto* bv_val = std::get_if<T>(&bv->v))
                            return av_val < *bv_val;
                    return false;
                }, av->v);
                return desc_order ? !less : less;
            });

        return [buf = std::move(buf), idx = size_t{0}]() mutable
               -> std::optional<iris::IrisValue> {
            if (idx >= buf.size()) return std::nullopt;
            return std::move(buf[idx++]);
        };
    }
    if (op == "select") {
        std::string field;
        if (auto* e = std::get_if<Expr>(&config))
            if (auto* fr = std::get_if<FieldRef>(e))
                field = fr->name;
        return [field, desc, up = std::move(upstream)]() mutable
               -> std::optional<iris::IrisValue> {
            while (auto v = up()) {
                auto fv = read_field(field, *v, desc);
                if (!fv) continue;
                // Produce a string-payload IrisValue for the selected field
                std::string s;
                std::visit([&](const auto& x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, std::string>)      s = x;
                    else if constexpr (std::is_same_v<T, int64_t>)     s = std::to_string(x);
                    else if constexpr (std::is_same_v<T, double>)      s = std::to_string(x);
                    else if constexpr (std::is_same_v<T, bool>)        s = x ? "true" : "false";
                }, fv->v);
                iris::IrisValue out;
                out.type_id = 0;
                out.payload = std::move(s);
                return out;
            }
            return std::nullopt;
        };
    }
    if (op == "collect") {
        std::vector<iris::IrisValue> buf;
        while (auto v = upstream()) buf.push_back(std::move(*v));
        return [buf = std::move(buf), idx = size_t{0}]() mutable
               -> std::optional<iris::IrisValue> {
            if (idx >= buf.size()) return std::nullopt;
            return std::move(buf[idx++]);
        };
    }
    if (op == "types") {
        std::vector<std::string> names;
        for (auto& [id, d] : global_.all())  names.push_back(d.name);
        for (auto& [id, d] : session_.all()) names.push_back(d.name);
        return [names = std::move(names), idx = size_t{0}]() mutable
               -> std::optional<iris::IrisValue> {
            if (idx >= names.size()) return std::nullopt;
            iris::IrisValue v; v.type_id = 0; v.payload = names[idx++]; return v;
        };
    }
    if (op == "type") {
        std::string tname;
        if (auto* s = std::get_if<std::string>(&config)) tname = *s;
        const auto* d = global_.find(tname);
        if (!d) d = session_.find(tname);
        std::vector<std::string> lines;
        if (d)
            for (auto& f : d->fields)
                lines.push_back(f.name + ": " + kind_name(f.kind));
        return [lines = std::move(lines), idx = size_t{0}]() mutable
               -> std::optional<iris::IrisValue> {
            if (idx >= lines.size()) return std::nullopt;
            iris::IrisValue v; v.type_id = 0; v.payload = lines[idx++]; return v;
        };
    }
    // map, print, write — passthrough; executor handles output
    if (op == "map" || op == "print" || op == "write")
        return upstream;

    return []() -> std::optional<iris::IrisValue> { return std::nullopt; };
}

} // namespace iris::irsh
