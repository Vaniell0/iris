#pragma once
#include "../parser/ast.hpp"
#include <sdk/cpp/types.hpp>
#include <value.hpp>
#include <optional>
#include <string>
#include <variant>

namespace iris::irsh {

struct EvalVal {
    std::variant<int64_t, double, bool, std::string> v;

    bool as_bool() const {
        return std::visit([](const auto& x) -> bool {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, bool>)        return x;
            if constexpr (std::is_same_v<T, int64_t>)     return x != 0;
            if constexpr (std::is_same_v<T, double>)      return x != 0.0;
            if constexpr (std::is_same_v<T, std::string>) return !x.empty();
        }, v);
    }
};

// Read one named field from an IrisValue.
// Pass desc=nullptr for text-line values (string payload); then only "text"/"line" are valid.
std::optional<EvalVal> read_field(std::string_view field,
                                   const iris::IrisValue& val,
                                   const iris::TypeDescriptor* desc);

// Evaluate a standalone Expr (no current stream value — for $args in config).
// args may be nullptr.
std::optional<EvalVal> eval_expr(const Expr& expr,
                                  const std::vector<std::string>* args = nullptr);

// Evaluate a predicate Expr against a value.
// Pass desc=nullptr when val holds a text line (string payload).
bool eval_predicate(const Expr& expr,
                    const iris::IrisValue& val,
                    const iris::TypeDescriptor* desc,
                    const std::vector<std::string>* args = nullptr);

} // namespace iris::irsh
