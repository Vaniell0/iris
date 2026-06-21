/// @file src/irish/backend/base_irsh.cpp
#include "base_irsh.hpp"
#include "../checker/checker.hpp"
#include "../exec/eval.hpp"
#include <registry.hpp>
#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>

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
                                           const TypeRegistry& global,
                                           const TypeRegistry* session = nullptr) {
    if (auto* s = std::get_if<StreamType>(&t)) {
        if (auto* d = global.find(s->elem_id)) return d;
        if (session) return session->find(s->elem_id);
    }
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
    const TypeRegistry* sess = &session_.session_types();
    if (op == "filter") {
        bool is_text = std::holds_alternative<TextLineType>(input);
        const TypeDescriptor* desc = resolve_desc(input, global, sess);
        if (!is_text && !desc)
            errs.push_back({loc, "filter: input is not a stream"});
        else if (auto* pred = std::get_if<Expr>(&config))
            walk_expr_fields(*pred, desc, is_text, errs);
        return input;
    }
    if (op == "sort") {
        if (auto* sa = std::get_if<SortArg>(&config)) {
            if (auto* desc = resolve_desc(input, global, sess))
                if (!desc->find_field(sa->field))
                    errs.push_back({loc,
                        "sort: type '" + desc->name + "' has no field '" + sa->field + "'"});
        }
        return input;
    }
    if (op == "select") {
        if (auto* e = std::get_if<Expr>(&config)) {
            if (auto* fr = std::get_if<FieldRef>(e)) {
                if (auto* desc = resolve_desc(input, global, sess)) {
                    if (auto* f = desc->find_field(fr->name))
                        return ScalarType{f->kind};
                    errs.push_back({loc,
                        "select: type '" + desc->name + "' has no field '" + fr->name + "'"});
                }
            }
        }
        return VoidType{};
    }
    if (op == "head") return input;
    if (op == "map") {
        if (auto* fields = std::get_if<std::vector<std::string>>(&config))
            if (auto* desc = resolve_desc(input, global, sess))
                for (auto& f : *fields)
                    if (!desc->find_field(f))
                        errs.push_back({loc,
                            "map: type '" + desc->name + "' has no field '" + f + "'"});
        return TextLineType{};
    }
    if (op == "collect") {
        if (auto* s = std::get_if<StreamType>(&input)) return VecType{s->elem_id};
        if (std::holds_alternative<TextLineType>(input)) return VecType{0};
        return input;
    }
    if (op == "print" || op == "write") return VoidType{};
    if (op == "lit")   return TextLineType{};
    if (op == "types") return TextLineType{};
    if (op == "type") {
        if (!std::get_if<std::string>(&config))
            errs.push_back({loc, "type: expected type name, e.g. type(DirEntry)"});
        return TextLineType{};
    }
    if (op == "parse") {
        // parse(TypeName) — TextLine → Stream<TypeName>
        if (auto* s = std::get_if<std::string>(&config)) {
            if (auto* d = global.find(*s))                      return StreamType{d->id};
            if (auto* d = session_.session_types().find(*s))    return StreamType{d->id};
            errs.push_back({loc, "parse: unknown type '" + *s + "'"});
        } else {
            errs.push_back({loc, "parse: expected type name, e.g. parse(DirEntry)"});
        }
        return VoidType{};
    }
    errs.push_back({loc, "@base." + std::string{op} + ": unknown operation"});
    return VoidType{};
}

// ── make_gen ──────────────────────────────────────────────────────────────────

IrisGen BaseIrshBackend::make_gen(std::string_view op,
                                   const BackendConfig& config,
                                   const TypeDescriptor* desc,
                                   IrisGen upstream) {
    const std::vector<std::string>* args_ptr = &session_.script_args();
    if (op == "lit") {
        std::string val;
        if (auto* s = std::get_if<std::string>(&config)) {
            val = *s;
        } else if (auto* e = std::get_if<Expr>(&config)) {
            if (auto ev = eval_expr(*e, args_ptr)) {
                std::visit([&](const auto& x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, std::string>) val = x;
                    else val = std::to_string(x);
                }, ev->v);
            }
        }
        bool done = false;
        return [val, done]() mutable -> IrisResult {
            if (done) return iris_end();
            done = true;
            iris::IrisValue v; v.type_id = 0; v.payload = val; return iris_val(std::move(v));
        };
    }
    if (op == "filter") {
        Expr pred = BoolLit{true, {}};
        if (auto* e = std::get_if<Expr>(&config)) pred = *e;
        return [pred, desc, args_ptr, up = std::move(upstream)]() mutable -> IrisResult {
            while (auto _r = up()) {
                if (!*_r) return iris_end();
                if (eval_predicate(pred, **_r, desc, args_ptr)) return iris_val(std::move(**_r));
            }
            return iris_end();
        };
    }
    if (op == "head") {
        int64_t limit = 10;
        if (auto* e = std::get_if<Expr>(&config)) {
            if (auto* il = std::get_if<IntLit>(e))
                limit = il->value;
            else if (auto* dv = std::get_if<DollarExpr>(e)) {
                if (auto ev = eval_expr(*e, args_ptr))
                    if (auto* n = std::get_if<std::string>(&ev->v))
                        limit = std::strtoll(n->c_str(), nullptr, 10);
                    else if (auto* n = std::get_if<int64_t>(&ev->v))
                        limit = *n;
                (void)dv;
            }
        }
        return [count = int64_t{0}, limit, up = std::move(upstream)]() mutable -> IrisResult {
            if (count >= limit) return iris_end();
            auto _r = up();
            if (!_r) return _r;      // propagate upstream error
            if (*_r) ++count;
            return _r;
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
        while (auto _r = upstream()) { if (!*_r) break; buf.push_back(std::move(**_r)); }

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

        return [buf = std::move(buf), idx = size_t{0}]() mutable -> IrisResult {
            if (idx >= buf.size()) return iris_end();
            return iris_val(std::move(buf[idx++]));
        };
    }
    if (op == "select") {
        std::string field;
        if (auto* e = std::get_if<Expr>(&config))
            if (auto* fr = std::get_if<FieldRef>(e))
                field = fr->name;
        return [field, desc, up = std::move(upstream)]() mutable -> IrisResult {
            while (auto _r = up()) {
                if (!*_r) return iris_end();
                auto fv = read_field(field, **_r, desc);
                if (!fv) continue;
                std::string s;
                std::visit([&](const auto& x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, std::string>)  s = x;
                    else if constexpr (std::is_same_v<T, int64_t>) s = std::to_string(x);
                    else if constexpr (std::is_same_v<T, double>)  s = std::to_string(x);
                    else if constexpr (std::is_same_v<T, bool>)    s = x ? "true" : "false";
                }, fv->v);
                iris::IrisValue out; out.type_id = 0; out.payload = std::move(s);
                return iris_val(std::move(out));
            }
            return iris_end();
        };
    }
    if (op == "collect") {
        std::vector<iris::IrisValue> buf;
        while (auto _r = upstream()) { if (!*_r) break; buf.push_back(std::move(**_r)); }
        return [buf = std::move(buf), idx = size_t{0}]() mutable -> IrisResult {
            if (idx >= buf.size()) return iris_end();
            return iris_val(std::move(buf[idx++]));
        };
    }
    if (op == "types") {
        std::vector<std::string> names;
        for (auto& [id, d] : global_.all())  names.push_back(d.name);
        for (auto& [id, d] : session_.session_types().all()) names.push_back(d.name);
        return [names = std::move(names), idx = size_t{0}]() mutable -> IrisResult {
            if (idx >= names.size()) return iris_end();
            iris::IrisValue v; v.type_id = 0; v.payload = names[idx++];
            return iris_val(std::move(v));
        };
    }
    if (op == "type") {
        std::string tname;
        if (auto* s = std::get_if<std::string>(&config)) tname = *s;
        const auto* d = global_.find(tname);
        if (!d) d = session_.session_types().find(tname);
        std::vector<std::string> lines;
        if (d)
            for (auto& f : d->fields)
                lines.push_back(f.name + ": " + kind_name(f.kind));
        return [lines = std::move(lines), idx = size_t{0}]() mutable -> IrisResult {
            if (idx >= lines.size()) return iris_end();
            iris::IrisValue v; v.type_id = 0; v.payload = lines[idx++];
            return iris_val(std::move(v));
        };
    }
    if (op == "map") {
        // Project selected fields to tab-separated text lines.
        // Typed struct projection (map returning a new struct type) requires
        // session-level TypeDescriptor registration — deferred to Part 2.x.
        auto fields = std::get_if<std::vector<std::string>>(&config);
        if (!fields || fields->empty()) return upstream;
        auto fnames = *fields;
        return [fnames, desc, up = std::move(upstream)]() mutable -> IrisResult {
            while (auto _r = up()) {
                if (!*_r) return iris_end();
                std::string line;
                for (size_t i = 0; i < fnames.size(); ++i) {
                    auto fv = read_field(fnames[i], **_r, desc);
                    if (i) line += '\t';
                    if (fv) std::visit([&](const auto& x) {
                        using T = std::decay_t<decltype(x)>;
                        if constexpr (std::is_same_v<T, std::string>) line += x;
                        else line += std::to_string(x);
                    }, fv->v);
                }
                iris::IrisValue out; out.type_id = 0; out.payload = std::move(line);
                return iris_val(std::move(out));
            }
            return iris_end();
        };
    }
    if (op == "parse") {
        // Split each TextLine by whitespace → fill struct fields in field order.
        std::string tname;
        if (auto* s = std::get_if<std::string>(&config)) tname = *s;
        const TypeDescriptor* td = global_.find(tname);
        if (!td) td = session_.session_types().find(tname);
        return [td, up = std::move(upstream)]() mutable -> IrisResult {
            while (auto _r = up()) {
                if (!*_r) return iris_end();
                if (!td) continue;
                std::string line;
                if ((*_r)->is_str()) line = std::get<std::string>((*_r)->payload);
                else continue;
                std::vector<std::string> tokens;
                std::istringstream iss{line};
                for (std::string tok; iss >> tok;) tokens.push_back(std::move(tok));
                if (tokens.empty()) continue;
                std::vector<uint8_t> buf(td->total_size, 0);
                for (size_t i = 0; i < td->fields.size() && i < tokens.size(); ++i) {
                    auto& f = td->fields[i];
                    auto dst = buf.data() + f.offset;
                    switch (f.kind) {
                        case iris::PrimitiveKind::Bool: {
                            uint8_t b = (tokens[i] == "true" || tokens[i] == "1") ? 1 : 0;
                            std::memcpy(dst, &b, 1);
                        } break;
                        case iris::PrimitiveKind::I8: {
                            int8_t n = static_cast<int8_t>(std::strtol(tokens[i].c_str(), nullptr, 10));
                            std::memcpy(dst, &n, 1);
                        } break;
                        case iris::PrimitiveKind::I16: {
                            int16_t n = static_cast<int16_t>(std::strtol(tokens[i].c_str(), nullptr, 10));
                            std::memcpy(dst, &n, 2);
                        } break;
                        case iris::PrimitiveKind::I32: {
                            int32_t n = static_cast<int32_t>(std::strtol(tokens[i].c_str(), nullptr, 10));
                            std::memcpy(dst, &n, 4);
                        } break;
                        case iris::PrimitiveKind::I64: {
                            int64_t n = std::strtoll(tokens[i].c_str(), nullptr, 10);
                            std::memcpy(dst, &n, std::min<size_t>(8, f.size));
                        } break;
                        case iris::PrimitiveKind::F64: {
                            double d = std::strtod(tokens[i].c_str(), nullptr);
                            std::memcpy(dst, &d, std::min<size_t>(8, f.size));
                        } break;
                        case iris::PrimitiveKind::F32: {
                            float d = std::strtof(tokens[i].c_str(), nullptr);
                            std::memcpy(dst, &d, std::min<size_t>(4, f.size));
                        } break;
                        case iris::PrimitiveKind::Str:
                        case iris::PrimitiveKind::CStr: {
                            // Fixed-size C-string stored in buffer (read_field reads via strnlen)
                            size_t cap = f.size > 0 ? f.size : tokens[i].size() + 1;
                            size_t len = std::min(tokens[i].size(), cap > 0 ? cap - 1 : 0);
                            std::memcpy(dst, tokens[i].data(), len);
                            if (cap > len) reinterpret_cast<char*>(dst)[len] = '\0';
                        } break;
                        default: break;
                    }
                }
                iris::IrisValue out;
                out.type_id = td->id;
                out.payload = iris::IrisBuffer::from(buf.data(), buf.size());
                return iris_val(std::move(out));
            }
            return iris_end();
        };
    }
    // print, write — passthrough; executor handles output
    if (op == "print" || op == "write")
        return upstream;

    return []() -> IrisResult { return iris_end(); };
}

} // namespace iris::irsh
