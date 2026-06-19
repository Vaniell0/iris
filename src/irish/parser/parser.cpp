/// @file src/irish/parser/parser.cpp
#include "parser.hpp"
#include <charconv>
#include <cstdlib>
#include <string>

namespace iris::irsh {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

// ── Primitives ────────────────────────────────────────────────────────────────

Token& Parser::peek(size_t ahead) {
    size_t i = pos_ + ahead;
    return tokens_[i < tokens_.size() ? i : tokens_.size() - 1];
}

Token& Parser::advance() {
    return tokens_[pos_ < tokens_.size() ? pos_++ : pos_];
}

bool Parser::check(TokenKind k) const {
    return tokens_[pos_].kind == k;
}

bool Parser::match(TokenKind k) {
    if (check(k)) { ++pos_; return true; }
    return false;
}

Token& Parser::expect(TokenKind k, std::string_view msg) {
    if (check(k)) return advance();
    emit_error(std::string{msg});
    return tokens_[pos_ < tokens_.size() ? pos_ : tokens_.size() - 1];
}

void Parser::emit_error(std::string msg) {
    errors_.push_back({{peek().line, peek().col}, std::move(msg)});
}

// ── Top level ─────────────────────────────────────────────────────────────────

ParseResult Parser::parse() {
    ParseResult result;
    while (!check(TokenKind::Eof))
        result.program.stmts.push_back(parse_statement());
    result.errors = std::move(errors_);
    return result;
}

Statement Parser::parse_statement() {
    if (check(TokenKind::KwLet))  return parse_let();
    if (check(TokenKind::KwType)) return parse_type_decl();
    return parse_pipeline();
}

// ── let ───────────────────────────────────────────────────────────────────────

LetStmt Parser::parse_let() {
    Loc loc{peek().line, peek().col};
    advance(); // let
    auto& name_tok = expect(TokenKind::Ident, "expected variable name after 'let'");
    std::string name{name_tok.text};
    expect(TokenKind::Assign, "expected '=' after variable name");
    auto rhs = parse_pipeline();
    return {std::move(name), std::move(rhs), loc};
}

// ── type declaration ──────────────────────────────────────────────────────────

TypeDecl Parser::parse_type_decl() {
    Loc loc{peek().line, peek().col};
    advance(); // type
    auto& name_tok = expect(TokenKind::Ident, "expected type name");
    std::string name{name_tok.text};
    expect(TokenKind::LBrace, "expected '{'");
    std::vector<TypeDecl::Field> fields;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        Loc floc{peek().line, peek().col};
        auto& fn  = expect(TokenKind::Ident,    "expected field name");
        expect(TokenKind::Colon, "expected ':' after field name");
        auto& fk  = expect(TokenKind::Ident,    "expected type kind");
        uint32_t sz = 0;
        if (match(TokenKind::LParen)) {
            auto& n = expect(TokenKind::Integer, "expected size");
            sz = static_cast<uint32_t>(std::strtoul(std::string{n.text}.c_str(), nullptr, 10));
            expect(TokenKind::RParen, "expected ')'");
        }
        match(TokenKind::Comma);
        fields.push_back({std::string{fn.text}, std::string{fk.text}, sz, floc});
    }
    expect(TokenKind::RBrace, "expected '}'");
    return {std::move(name), std::move(fields), loc};
}

// ── pipeline ──────────────────────────────────────────────────────────────────

Pipeline Parser::parse_pipeline() {
    Loc loc{peek().line, peek().col};
    auto source = parse_source();
    std::vector<Stage> stages;
    while (match(TokenKind::Pipe))
        stages.push_back(parse_stage());
    return {std::move(source), std::move(stages), loc};
}

// @ns.op(config)  or  @ns(config)
BackendRef Parser::parse_source() {
    Loc loc{peek().line, peek().col};
    expect(TokenKind::At, "expected '@'");
    auto& ns_tok = expect(TokenKind::Ident, "expected backend namespace");
    std::string ns{ns_tok.text};
    std::string op;
    if (match(TokenKind::Dot)) {
        auto& op_tok = expect(TokenKind::Ident, "expected operation name");
        op = std::string{op_tok.text};
    }
    std::string config;
    if (match(TokenKind::LParen)) {
        while (!check(TokenKind::RParen) && !check(TokenKind::Eof))
            config += advance().text;   // no spaces — paths like .. or ../foo must stay intact
        expect(TokenKind::RParen, "expected ')'");
    }
    return {std::move(ns), std::move(op), std::move(config), loc};
}

// ── stages ────────────────────────────────────────────────────────────────────

Stage Parser::parse_stage() {
    if (check(TokenKind::At)) return BackendStage{parse_source()};
    if (!check(TokenKind::Ident)) {
        emit_error("expected stage name");
        advance();
        return FilterStage{BoolLit{true, {}}, {}};
    }
    auto sv = peek().text;
    if (sv == "filter")  return parse_filter();
    if (sv == "sort")    return parse_sort();
    if (sv == "map")     return parse_map();
    if (sv == "select")  return parse_select();
    if (sv == "head")    return parse_head();
    if (sv == "collect") { Loc l{peek().line, peek().col}; advance(); return CollectStage{l}; }
    if (sv == "print")   { Loc l{peek().line, peek().col}; advance(); return PrintStage{l}; }
    if (sv == "write") {
        Loc l{peek().line, peek().col};
        advance();
        expect(TokenKind::LParen, "expected '('");
        auto& p = expect(TokenKind::PathLiteral, "expected path");
        std::string path{p.text};
        expect(TokenKind::RParen, "expected ')'");
        return WriteStage{std::move(path), l};
    }
    emit_error("unknown stage '" + std::string{sv} + "'");
    advance();
    return FilterStage{BoolLit{true, {}}, {}};
}

BackendStage Parser::parse_backend_stage() {
    return {parse_source()};
}

FilterStage Parser::parse_filter() {
    Loc loc{peek().line, peek().col};
    advance(); // filter
    expect(TokenKind::LParen, "expected '('");
    auto pred = parse_expr();
    expect(TokenKind::RParen, "expected ')'");
    return {std::move(pred), loc};
}

SortStage Parser::parse_sort() {
    Loc loc{peek().line, peek().col};
    advance(); // sort
    expect(TokenKind::LParen, "expected '('");
    match(TokenKind::KwBy);
    auto& f = expect(TokenKind::Ident, "expected field name");
    std::string field{f.text};
    bool desc = (check(TokenKind::Ident) && peek().text == "desc") ? (advance(), true) : false;
    expect(TokenKind::RParen, "expected ')'");
    return {std::move(field), desc, loc};
}

MapStage Parser::parse_map() {
    Loc loc{peek().line, peek().col};
    advance(); // map
    expect(TokenKind::LParen, "expected '('");
    std::vector<std::string> fields;
    while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
        auto& f = expect(TokenKind::Ident, "expected field name");
        fields.push_back(std::string{f.text});
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RParen, "expected ')'");
    return {std::move(fields), loc};
}

SelectStage Parser::parse_select() {
    Loc loc{peek().line, peek().col};
    advance(); // select
    expect(TokenKind::LParen, "expected '('");
    auto& f = expect(TokenKind::Ident, "expected field name");
    std::string field{f.text};
    expect(TokenKind::RParen, "expected ')'");
    return {std::move(field), loc};
}

HeadStage Parser::parse_head() {
    Loc loc{peek().line, peek().col};
    advance(); // head
    int64_t n = 10;
    if (check(TokenKind::Integer)) {
        n = std::strtoll(std::string{advance().text}.c_str(), nullptr, 10);
    } else if (match(TokenKind::LParen)) {
        auto& t = expect(TokenKind::Integer, "expected integer");
        n = std::strtoll(std::string{t.text}.c_str(), nullptr, 10);
        expect(TokenKind::RParen, "expected ')'");
    }
    return {n, loc};
}

// ── expressions ───────────────────────────────────────────────────────────────

Expr Parser::parse_expr() { return parse_comparison(); }

Expr Parser::parse_comparison() {
    auto lhs = parse_unary();
    while (true) {
        BinOpKind op;
        Loc loc{peek().line, peek().col};
        if      (match(TokenKind::EqEq))  op = BinOpKind::Eq;
        else if (match(TokenKind::NotEq)) op = BinOpKind::Ne;
        else if (match(TokenKind::LtEq))  op = BinOpKind::Le;
        else if (match(TokenKind::GtEq))  op = BinOpKind::Ge;
        else if (match(TokenKind::Lt))    op = BinOpKind::Lt;
        else if (match(TokenKind::Gt))    op = BinOpKind::Gt;
        else if (match(TokenKind::AndAnd))op = BinOpKind::And;
        else if (match(TokenKind::OrOr))  op = BinOpKind::Or;
        else break;
        auto rhs = parse_unary();
        lhs = std::make_shared<BinOp>(op, std::move(lhs), std::move(rhs), loc);
    }
    return lhs;
}

Expr Parser::parse_unary() {
    if (check(TokenKind::Bang)) {
        Loc loc{peek().line, peek().col};
        advance();
        return std::make_shared<UnOp>(parse_unary(), loc);
    }
    return parse_primary();
}

Expr Parser::parse_primary() {
    Loc loc{peek().line, peek().col};
    if (check(TokenKind::Integer)) {
        auto& t = advance();
        return IntLit{std::strtoll(std::string{t.text}.c_str(), nullptr, 10), loc};
    }
    if (check(TokenKind::Float)) {
        auto& t = advance();
        return FloatLit{std::strtod(std::string{t.text}.c_str(), nullptr), loc};
    }
    if (check(TokenKind::String)) {
        auto& t = advance();
        auto sv = t.text;
        if (sv.size() >= 2) sv = sv.substr(1, sv.size() - 2);
        return StrLit{std::string{sv}, loc};
    }
    if (check(TokenKind::Bool)) {
        auto& t = advance();
        return BoolLit{t.text == "true", loc};
    }
    // keywords used as field names in filter predicates (e.g. filter(type == 1))
    if (check(TokenKind::Ident) || check(TokenKind::KwType) ||
        check(TokenKind::KwBy)  || check(TokenKind::KwLet)) {
        auto& t = advance();
        return FieldRef{std::string{t.text}, loc};
    }
    if (match(TokenKind::LParen)) {
        auto e = parse_expr();
        expect(TokenKind::RParen, "expected ')'");
        return e;
    }
    emit_error("expected expression");
    if (!check(TokenKind::Eof)) advance();
    return IntLit{0, loc};
}

} // namespace iris::irsh
