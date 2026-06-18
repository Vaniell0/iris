#pragma once
#include "ast.hpp"
#include "../lexer/token.hpp"
#include <span>
#include <string>
#include <vector>

namespace iris::irsh {

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
    BackendRef  parse_source();
    Stage       parse_stage();
    BackendStage parse_backend_stage();
    FilterStage parse_filter();
    SortStage   parse_sort();
    MapStage    parse_map();
    SelectStage parse_select();
    HeadStage   parse_head();
    Expr        parse_expr();
    Expr        parse_comparison();
    Expr        parse_unary();
    Expr        parse_primary();

    void        emit_error(std::string msg);
    std::vector<ParseError> errors_;
};

} // namespace iris::irsh
