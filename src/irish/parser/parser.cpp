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
    while (!check(TokenKind::Eof)) {
        size_t before = pos_;
        result.program.stmts.push_back(parse_statement());
        if (pos_ == before) { emit_error("unexpected token"); advance(); }
    }
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
    if (!match(TokenKind::LBrace)) {
        emit_error("expected '{'");
        return {std::move(name), {}, loc};
    }
    std::vector<TypeDecl::Field> fields;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        if (!check(TokenKind::Ident)) {
            emit_error("expected field name or '}'");
            advance();
            break;
        }
        Loc floc{peek().line, peek().col};
        auto& fn = advance(); // Ident confirmed above
        expect(TokenKind::Colon, "expected ':' after field name");
        if (!check(TokenKind::Ident)) {
            emit_error("expected type kind");
            break;
        }
        auto& fk = advance(); // Ident confirmed above
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

// ── parse config helpers ──────────────────────────────────────────────────────

// ── source ────────────────────────────────────────────────────────────────────
//
// Full form: @ns.op or @ns.op("config")
// Bare-word sugar: from k_shorthands where as_source == true
// ./path  → @os.exec(path)

BackendCall Parser::parse_source() {
    Loc loc{peek().line, peek().col};

    // ./path — sugar for @os.exec
    if (check(TokenKind::PathLiteral)) {
        auto path = std::string{advance().text};
        return {"os", "exec", std::move(path), loc};
    }

    if (check(TokenKind::Ident)) {
        auto sv = peek().text;

        // Bare-word shorthand — look up in canonical table (single source of truth)
        for (auto& sh : k_shorthands) {
            if (!sh.as_source || sh.name != sv) continue;
            advance();
            BackendConfig config;
            // ls accepts optional flags (-la) and/or an optional path argument
            if (sh.op == "ls") {
                std::string flags, path;
                if (check(TokenKind::FlagStr))
                    flags = std::string{advance().text};
                if (check(TokenKind::String) || check(TokenKind::PathLiteral))
                    path = std::string{advance().text};
                else if (check(TokenKind::LParen)) {
                    advance();
                    std::string raw;
                    while (!check(TokenKind::RParen) && !check(TokenKind::Eof))
                        raw += advance().text;
                    expect(TokenKind::RParen, "expected ')'");
                    path = std::move(raw);
                }
                if (!flags.empty() || !path.empty())
                    config = flags + (flags.empty() || path.empty() ? "" : " ") + path;
            }
            return {std::string{sh.ns}, std::string{sh.op}, std::move(config), loc};
        }

        // Unknown identifier — session variable reference
        std::string vname{sv};
        advance();
        return {"_var", std::move(vname), {}, loc};
    }

    // Full @ns.op or @ns.op("config") form
    expect(TokenKind::At, "expected '@' or a source shorthand");
    auto& ns_tok = expect(TokenKind::Ident, "expected backend namespace");
    std::string ns{ns_tok.text};
    std::string op;
    if (match(TokenKind::Dot)) {
        // Accept any token as op name — keywords (type, by) are valid op names in @ns.op
        if (!check(TokenKind::Eof)) { op = std::string{peek().text}; advance(); }
        else emit_error("expected operation name");
    }
    BackendConfig config;
    if (check(TokenKind::String) || check(TokenKind::PathLiteral)) {
        config = std::string{advance().text};
    } else if (match(TokenKind::LParen)) {
        std::string raw;
        while (!check(TokenKind::RParen) && !check(TokenKind::Eof))
            raw += advance().text;
        expect(TokenKind::RParen, "expected ')'");
        config = std::move(raw);
    }
    return {std::move(ns), std::move(op), std::move(config), loc};
}

// ── stages ────────────────────────────────────────────────────────────────────
//
// Every stage is a BackendCall.  Shorthands map to @base.* operations.
// If the stage starts with @, it is a full backend call (possibly @base.filter, etc.)

BackendCall Parser::parse_stage() {
    Loc loc{peek().line, peek().col};

    // Path literal in stage position — sugar for @os.exec
    if (check(TokenKind::PathLiteral)) {
        auto path = std::string{advance().text};
        return {"os", "exec", std::move(path), loc};
    }

    // Full @ns.op form in stage position
    if (check(TokenKind::At)) {
        advance(); // @
        auto& ns_tok = expect(TokenKind::Ident, "expected backend namespace");
        std::string ns{ns_tok.text};
        std::string op;
        if (match(TokenKind::Dot)) {
            // Accept any token as op name — keywords like 'type' are valid @ns.op ops
            if (!check(TokenKind::Eof)) { op = std::string{peek().text}; advance(); }
            else emit_error("expected operation name");
        }
        // For @base.* ops, parse typed config; otherwise raw string
        if (ns == "base") {
            BackendConfig config = parse_base_stage_config(op, loc);
            return {std::move(ns), std::move(op), std::move(config), loc};
        }
        // Generic: optional raw-string config in parens
        BackendConfig config;
        if (match(TokenKind::LParen)) {
            std::string raw;
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof))
                raw += advance().text;
            expect(TokenKind::RParen, "expected ')'");
            config = std::move(raw);
        }
        return {std::move(ns), std::move(op), std::move(config), loc};
    }

    // 'type' is a keyword token but also a valid @base stage op.
    if (check(TokenKind::KwType)) {
        advance();
        BackendConfig config = parse_base_stage_config("type", loc);
        return {"base", "type", std::move(config), loc};
    }

    if (!check(TokenKind::Ident)) {
        emit_error("expected stage name");
        advance();
        return {"base", "filter", Expr{BoolLit{true, {}}}, loc};
    }

    auto sv = peek().text;

    // Bare-word shorthand — canonical table is the single source of truth
    for (auto& sh : k_shorthands) {
        if (!sh.as_stage || sh.name != sv) continue;
        advance();
        BackendConfig config;
        if (sh.ns == "base") config = parse_base_stage_config(sh.op, loc);
        return {std::string{sh.ns}, std::string{sh.op}, std::move(config), loc};
    }

    emit_error("unknown stage '" + std::string{sv} + "'");
    advance();
    return {"base", "filter", Expr{BoolLit{true, {}}}, loc};
}

// Parse the config for a known @base.* operation (called after consuming the op name)
BackendConfig Parser::parse_base_stage_config(std::string_view op, Loc /*loc*/) {
    if (op == "filter") {
        return Expr{parse_expr()};
    }
    if (op == "sort") {
        // sort [by:] <field> [desc]
        if (check(TokenKind::KwBy)) {
            advance();
            expect(TokenKind::Colon, "expected ':' after 'by'");
        }
        auto& f    = expect(TokenKind::Ident, "expected sort field name");
        bool  desc = check(TokenKind::Ident) && peek().text == "desc"
                         ? (advance(), true) : false;
        return SortArg{std::string{f.text}, desc};
    }
    if (op == "select") {
        auto& f = expect(TokenKind::Ident, "expected field name");
        return Expr{FieldRef{std::string{f.text}, {}}};
    }
    if (op == "map") {
        expect(TokenKind::LBrace, "expected '{'");
        std::vector<std::string> fields;
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            // Accept any token as a field name — 'type' is a keyword but a valid field.
            auto fname = std::string{peek().text}; advance();
            fields.push_back(std::move(fname));
            if (!match(TokenKind::Comma)) break;
        }
        expect(TokenKind::RBrace, "expected '}'");
        return fields;
    }
    if (op == "head") {
        int64_t n = 10;
        if (check(TokenKind::Integer)) {
            n = std::strtoll(std::string{advance().text}.c_str(), nullptr, 10);
        } else if (match(TokenKind::LParen)) {
            auto& t = expect(TokenKind::Integer, "expected integer");
            n = std::strtoll(std::string{t.text}.c_str(), nullptr, 10);
            expect(TokenKind::RParen, "expected ')'");
        }
        return Expr{IntLit{n, {}}};
    }
    if (op == "write") {
        std::string path;
        if (check(TokenKind::String)) {
            auto raw = advance().text;
            if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
                path = std::string{raw.substr(1, raw.size() - 2)};
            else
                path = std::string{raw};
        } else if (check(TokenKind::PathLiteral)) {
            path = std::string{advance().text};
        } else {
            emit_error("write: expected path string");
        }
        return std::string{std::move(path)};
    }
    if (op == "type") {
        // Required parenthesised type name: type(DirEntry) or @base.type(DirEntry)
        if (match(TokenKind::LParen)) {
            std::string raw;
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof))
                raw += advance().text;
            expect(TokenKind::RParen, "expected ')'");
            return raw;
        }
        emit_error("type: expected type name, e.g. type(DirEntry)");
        return std::monostate{};
    }
    // collect, print, write, types — no config
    return std::monostate{};
}

// ── expressions ───────────────────────────────────────────────────────────────

Expr Parser::parse_expr() { return parse_or(); }

Expr Parser::parse_or() {
    auto lhs = parse_and();
    while (check(TokenKind::OrOr)) {
        Loc loc{peek().line, peek().col}; advance();
        auto rhs = parse_and();
        lhs = std::make_shared<BinOp>(BinOpKind::Or, std::move(lhs), std::move(rhs), loc);
    }
    return lhs;
}

Expr Parser::parse_and() {
    auto lhs = parse_comparison();
    while (check(TokenKind::AndAnd)) {
        Loc loc{peek().line, peek().col}; advance();
        auto rhs = parse_comparison();
        lhs = std::make_shared<BinOp>(BinOpKind::And, std::move(lhs), std::move(rhs), loc);
    }
    return lhs;
}

Expr Parser::parse_comparison() {
    auto lhs = parse_unary();
    Loc loc{peek().line, peek().col};
    BinOpKind op;
    if      (match(TokenKind::EqEq))  op = BinOpKind::Eq;
    else if (match(TokenKind::NotEq)) op = BinOpKind::Ne;
    else if (match(TokenKind::LtEq))  op = BinOpKind::Le;
    else if (match(TokenKind::GtEq))  op = BinOpKind::Ge;
    else if (match(TokenKind::Lt))    op = BinOpKind::Lt;
    else if (match(TokenKind::Gt))    op = BinOpKind::Gt;
    else if (check(TokenKind::Ident)) {
        auto sv2 = peek().text;
        if      (sv2 == "contains")    { advance(); op = BinOpKind::Contains;   }
        else if (sv2 == "starts_with") { advance(); op = BinOpKind::StartsWith; }
        else if (sv2 == "ends_with")   { advance(); op = BinOpKind::EndsWith;   }
        else if (sv2 == "matches")     { advance(); op = BinOpKind::Matches;    }
        else return lhs;
    }
    else return lhs;
    auto rhs = parse_unary();
    return std::make_shared<BinOp>(op, std::move(lhs), std::move(rhs), loc);
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
