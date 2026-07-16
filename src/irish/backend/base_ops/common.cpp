/// @file src/irish/backend/base_ops/common.cpp
/// Shared helpers used by more than one @base op.
#include "common.hpp"
#include <registry.hpp>

namespace iris::irsh::base_ops {

const char* kind_name(iris::PrimitiveKind k) {
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

const iris::TypeDescriptor* resolve_desc(const IrType& t,
                                          const iris::TypeRegistry& global,
                                          const iris::TypeRegistry* session) {
    if (auto* s = std::get_if<StreamType>(&t)) {
        if (auto* d = global.find(s->elem_id)) return d;
        if (session) return session->find(s->elem_id);
    }
    return nullptr;
}

void walk_expr_fields(const Expr& expr,
                       const iris::TypeDescriptor* desc,
                       bool is_text,
                       std::vector<TypeError>& errors) {
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

} // namespace iris::irsh::base_ops
