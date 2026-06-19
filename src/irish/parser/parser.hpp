#pragma once
#include "ast.hpp"
#include "../lexer/token.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace iris::irsh {

// ── Shorthand table ───────────────────────────────────────────────────────────
//
// Single source of truth for all bare-word sugar.
// Parser and REPL completion both read from here — never hardcode elsewhere.
//
// Syntax rules from spec:
//   @ns.op(args)  canonical form
//   bare word     sugar for @ns.op where as_source or as_stage is set
//   |             chains stages (sugar for piping)
//   ./path        sugar for @os.exec(path)     [PathLiteral token]

struct Shorthand {
    std::string_view name;      // bare word
    std::string_view ns;        // maps to @ns
    std::string_view op;        // .op
    bool             as_source; // valid before first |
    bool             as_stage;  // valid after |
};

inline constexpr Shorthand k_shorthands[] = {
    // OS sources — bare names from spec
    {"ls",      "os",   "ls",      true,  false},
    {"ps",      "os",   "ps",      true,  false},
    {"env",     "os",   "env",     true,  false},
    {"clear",   "os",   "clear",   true,  false},
    // Base — available in both positions
    {"types",   "base", "types",   true,  true },
    // Base stage ops
    {"filter",  "base", "filter",  false, true },
    {"sort",    "base", "sort",    false, true },
    {"select",  "base", "select",  false, true },
    {"map",     "base", "map",     false, true },
    {"head",    "base", "head",    false, true },
    {"collect", "base", "collect", false, true },
    {"print",   "base", "print",   false, true },
    {"write",   "base", "write",   false, true },
};



struct ParseError {
    Loc         loc;
    std::string msg;
};

struct ParseResult {
    Program              program;
    std::vector<ParseError> errors;
    bool ok() const { return errors.empty(); }
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    ParseResult parse();

private:
    std::vector<Token> tokens_;
    size_t             pos_ = 0;

    Token&      peek(size_t ahead = 0);
    Token&      advance();
    bool        check(TokenKind) const;
    bool        match(TokenKind);
    Token&      expect(TokenKind, std::string_view msg);

    Statement   parse_statement();
    LetStmt     parse_let();
    TypeDecl    parse_type_decl();
    Pipeline    parse_pipeline();
    BackendCall   parse_source();   // @ns.op or shorthand (ls/ps/env)
    BackendCall   parse_stage();    // returns BackendCall for any stage form
    BackendConfig parse_base_stage_config(std::string_view op, Loc loc);

    Expr        parse_expr();
    Expr        parse_or();
    Expr        parse_and();
    Expr        parse_comparison();
    Expr        parse_unary();
    Expr        parse_primary();

    void        emit_error(std::string msg);
    std::vector<ParseError> errors_;
};

} // namespace iris::irsh
