/// @file src/irish/parser/parser.cpp
#include "parser.hpp"

namespace iris::irsh {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

// ── TODO: implement ───────────────────────────────────────────────────────────

ParseResult Parser::parse() { return {}; }

Token& Parser::peek(size_t ahead) { return tokens_[std::min(pos_ + ahead, tokens_.size() - 1)]; }
Token& Parser::advance()          { return tokens_[pos_ < tokens_.size() ? pos_++ : pos_]; }
bool   Parser::check(TokenKind k) const { return tokens_[pos_].kind == k; }
bool   Parser::match(TokenKind k) { if (check(k)) { ++pos_; return true; } return false; }
Token& Parser::expect(TokenKind, std::string_view) { return tokens_[pos_]; }

void        Parser::emit_error(std::string msg) { errors_.push_back({{peek().line, peek().col}, std::move(msg)}); }

Statement   Parser::parse_statement()  { return {}; }
LetStmt     Parser::parse_let()        { return {}; }
TypeDecl    Parser::parse_type_decl()  { return {}; }
Pipeline    Parser::parse_pipeline()   { return {}; }
BackendRef  Parser::parse_source()     { return {}; }
Stage       Parser::parse_stage()      { return FilterStage{}; }
BackendStage Parser::parse_backend_stage() { return {}; }
FilterStage Parser::parse_filter()     { return {}; }
SortStage   Parser::parse_sort()       { return {}; }
MapStage    Parser::parse_map()        { return {}; }
SelectStage Parser::parse_select()     { return {}; }
HeadStage   Parser::parse_head()       { return {}; }
Expr        Parser::parse_expr()       { return IntLit{0, {}}; }
Expr        Parser::parse_comparison() { return IntLit{0, {}}; }
Expr        Parser::parse_unary()      { return IntLit{0, {}}; }
Expr        Parser::parse_primary()    { return IntLit{0, {}}; }

} // namespace iris::irsh
