/// @file src/irish/parser/parser.cpp
#include "parser.hpp"
#include <charconv>
#include <cstdlib>
#include <string>

namespace iris::irsh {

// Returns true if `next` starts immediately after `prev` in source (no whitespace between).
static bool adjacent(std::string_view prev, std::string_view next) {
    return !prev.empty() && !next.empty() && prev.data() + prev.size() == next.data();
}

Parser::Parser(std::vector<Token> tokens, ImportTable imports)
    : tokens_(std::move(tokens)), imports_(std::move(imports)) {}

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
        while (match(TokenKind::Semi)) {}
        if (check(TokenKind::Eof)) break;
        // Consolidate consecutive Error tokens into a single diagnostic
        if (check(TokenKind::Error)) {
            Loc loc{peek().line, peek().col};
            while (check(TokenKind::Error)) advance();
            errors_.push_back({loc, "unexpected character in input"});
            continue;
        }
        size_t before = pos_;
        result.program.stmts.push_back(parse_statement());
        if (pos_ == before) { emit_error("unexpected token"); advance(); }
    }
    result.errors = std::move(errors_);
    return result;
}

Statement Parser::parse_statement() {
    if (check(TokenKind::KwLet))    return parse_let();
    if (check(TokenKind::KwType))   return parse_type_decl();
    if (check(TokenKind::KwImport)) return parse_import();
    Loc loc{peek().line, peek().col};
    auto first = parse_pipeline();
    if (match(TokenKind::FireForget))
        return ParallelStmt{{std::move(first)}, {}, true, loc};
    if (check(TokenKind::ParallelPipe)) {
        std::vector<Pipeline> arms;
        arms.push_back(std::move(first));
        while (match(TokenKind::ParallelPipe))
            arms.push_back(parse_pipeline());
        return ParallelStmt{std::move(arms), {}, false, loc};
    }
    return first;
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
        } else if (match(TokenKind::LBracket)) {
            auto& n = expect(TokenKind::Integer, "expected size");
            sz = static_cast<uint32_t>(std::strtoul(std::string{n.text}.c_str(), nullptr, 10));
            expect(TokenKind::RBracket, "expected ']'");
        }
        match(TokenKind::Comma);
        fields.push_back({std::string{fn.text}, std::string{fk.text}, sz, floc});
    }
    expect(TokenKind::RBrace, "expected '}'");
    return {std::move(name), std::move(fields), loc};
}

// ── import ────────────────────────────────────────────────────────────────────

ImportStmt Parser::parse_import() {
    Loc loc{peek().line, peek().col};
    advance(); // import
    expect(TokenKind::At, "expected '@' after 'import'");
    auto& first = expect(TokenKind::Ident, "expected backend namespace");
    std::string ns{first.text};
    while (check(TokenKind::Dot)) {
        advance();
        if (check(TokenKind::Eof)) break;
        ns += '.';
        ns += peek().text;
        advance();
    }
    return {std::move(ns), loc};
}

// ── parse_dotted_path ─────────────────────────────────────────────────────────
//
// Consumes Ident(.token)* after @ has already been consumed.
// Returns {ns, op}: last segment = op, preceding segments joined with '.' = ns.
// Single segment (no dot) → {ns=segment, op=""} — "leaf backend" form.

std::pair<std::string, std::string> Parser::parse_dotted_path() {
    auto& first = expect(TokenKind::Ident, "expected backend namespace");
    std::string first_seg{first.text};
    if (!check(TokenKind::Dot))
        return {std::move(first_seg), ""};
    std::vector<std::string> segs;
    segs.push_back(std::move(first_seg));
    while (check(TokenKind::Dot)) {
        advance(); // consume dot
        if (check(TokenKind::Eof)) { emit_error("expected op name"); break; }
        segs.push_back(std::string{peek().text});
        advance();
    }
    std::string op = segs.back(); segs.pop_back();
    std::string ns;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (i) ns += '.';
        ns += segs[i];
    }
    return {ns, op};
}

// ── pipeline ──────────────────────────────────────────────────────────────────

Pipeline Parser::parse_pipeline() {
    Loc loc{peek().line, peek().col};
    auto source = parse_source();
    std::vector<Stage> stages;
    while (match(TokenKind::Pipe))
        stages.push_back(parse_stage());
    Pipeline result{std::move(source), std::move(stages), loc, {}, {}};
    if (match(TokenKind::FallbackVal)) {
        result.fallback_val = parse_expr();
    } else if (match(TokenKind::FallbackPipe)) {
        result.fallback_pipe = std::make_shared<Pipeline>(parse_pipeline());
    }
    return result;
}

// ── parse config helpers ──────────────────────────────────────────────────────

// ── source ────────────────────────────────────────────────────────────────────
//
// Full form: @ns.op or @ns.op("config")
// Bare-word sugar: from k_shorthands where as_source == true
// ./path  → @os.exec(path)

BackendCall Parser::parse_source() {
    Loc loc{peek().line, peek().col};

    // ./path or /abs or ~/home — exec sugar; collect trailing args
    if (check(TokenKind::PathLiteral)) {
        std::string_view first_sv = peek().text;
        auto path = std::string{advance().text};
        auto is_arg = [](TokenKind k) {
            switch (k) {
            case TokenKind::Ident: case TokenKind::String: case TokenKind::Integer:
            case TokenKind::Float: case TokenKind::Bool:  case TokenKind::FlagStr:
            case TokenKind::PathLiteral: case TokenKind::Dollar:
                return true;
            default: return false;
            }
        };
        if (!is_arg(peek().kind))
            return {"os", "exec", std::move(path), loc};
        ExecArgs args;
        std::string_view last_sv = first_sv;
        args.push_back({false, std::move(path)});
        while (is_arg(peek().kind)) {
            if (peek().kind == TokenKind::Dollar) {
                last_sv = {};
                advance();
                std::string name;
                if (check(TokenKind::Ident)) name = std::string{advance().text};
                args.push_back({true, std::move(name)});
            } else {
                const Token& tok = peek();
                if (tok.kind == TokenKind::PathLiteral && !args.empty() &&
                    !args.back().is_var && adjacent(last_sv, tok.text)) {
                    last_sv = tok.text;
                    args.back().text += std::string{advance().text};
                } else {
                    last_sv = tok.text;
                    advance();
                    std::string text{tok.text};
                    if (tok.kind == TokenKind::String && text.size() >= 2 &&
                        text.front() == '"' && text.back() == '"')
                        text = text.substr(1, text.size() - 2);
                    args.push_back({false, std::move(text)});
                }
            }
        }
        return {"os", "exec", std::move(args), loc};
    }

    // String/integer/bool/dollar literal in source position → @base.lit
    if (check(TokenKind::String) || check(TokenKind::Integer) ||
        check(TokenKind::Float)  || check(TokenKind::Bool)    ||
        check(TokenKind::Dollar)) {
        auto e = parse_primary();
        // Evaluate literal immediately to a string for simple cases
        std::string val;
        std::visit([&](const auto& n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, StrLit>)   val = n.value;
            else if constexpr (std::is_same_v<T, IntLit>)  val = std::to_string(n.value);
            else if constexpr (std::is_same_v<T, FloatLit>) val = std::to_string(n.value);
            else if constexpr (std::is_same_v<T, BoolLit>)  val = n.value ? "true" : "false";
            // DollarExpr resolved at exec time by executor
        }, e);
        if (auto* de = std::get_if<DollarExpr>(&e))
            return {"base", "lit", Expr{*de}, loc};
        return {"base", "lit", std::move(val), loc};
    }

    if (check(TokenKind::Ident)) {
        auto sv = peek().text;

        // Bare-word: look up in auto-import table (built from session.imports + registry)
        for (auto& entry : imports_) {
            if (!entry.as_source || entry.op != sv) continue;
            advance();
            BackendConfig config = parse_config(entry.config, loc);
            return {entry.ns, entry.op, std::move(config), loc};
        }

        // Not in imports. If followed by argument-like tokens, treat as PATH exec sugar:
        // `nix profile list`, `git log --oneline`, `cat ./file`, etc.
        std::string cmd{sv};
        advance();

        auto is_arg = [](TokenKind k) {
            switch (k) {
            case TokenKind::Ident:
            case TokenKind::String:
            case TokenKind::Integer:
            case TokenKind::Float:
            case TokenKind::Bool:
            case TokenKind::FlagStr:
            case TokenKind::PathLiteral:
            case TokenKind::Dollar:
                return true;
            default:
                return false;
            }
        };

        if (is_arg(peek().kind)) {
            ExecArgs args;
            std::string_view last_sv = sv; // track original string_view for adjacency check
            args.push_back({false, std::move(cmd)});
            while (is_arg(peek().kind)) {
                if (peek().kind == TokenKind::Dollar) {
                    last_sv = {};
                    advance();
                    std::string name;
                    if (check(TokenKind::Ident)) name = std::string{advance().text};
                    args.push_back({true, std::move(name)});
                } else {
                    const Token& tok = peek();
                    // Glue adjacent PathLiteral onto previous arg (e.g. GoodNet + /**)
                    if (tok.kind == TokenKind::PathLiteral && !args.empty() &&
                        !args.back().is_var && adjacent(last_sv, tok.text)) {
                        last_sv = tok.text;
                        args.back().text += std::string{advance().text};
                    } else {
                        last_sv = tok.text;
                        advance();
                        std::string text{tok.text};
                        if (tok.kind == TokenKind::String && text.size() >= 2 &&
                            text.front() == '"' && text.back() == '"')
                            text = text.substr(1, text.size() - 2);
                        args.push_back({false, std::move(text)});
                    }
                }
            }
            return {"os", "exec", std::move(args), loc};
        }

        // Single word — session variable reference
        return {"_var", std::move(cmd), {}, loc};
    }

    // Full @ns.op, @ns.sub.op, or @ns (leaf backend) form
    expect(TokenKind::At, "expected '@' or a source name");
    auto [ns, op] = parse_dotted_path();
    BackendConfig config;
    {
        auto ck = IrshBackend::ConfigKind::None;
        for (auto& e : imports_) if (e.ns == ns && e.op == op) { ck = e.config; break; }
        if (ck != IrshBackend::ConfigKind::None) {
            config = parse_config(ck, loc);
        } else if (check(TokenKind::String) || check(TokenKind::PathLiteral)) {
            config = std::string{advance().text};
        } else if (match(TokenKind::LParen)) {
            std::string raw;
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) raw += advance().text;
            expect(TokenKind::RParen, "expected ')'");
            config = std::move(raw);
        }
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

    // Full @ns.op, @ns.sub.op, or @ns form in stage position
    if (check(TokenKind::At)) {
        advance(); // @
        auto [ns, op] = parse_dotted_path();
        BackendConfig config;
        auto ck = IrshBackend::ConfigKind::None;
        for (auto& e : imports_) if (e.ns == ns && e.op == op) { ck = e.config; break; }
        if (ck != IrshBackend::ConfigKind::None) {
            config = parse_config(ck, loc);
        } else if (check(TokenKind::String) || check(TokenKind::PathLiteral)) {
            config = std::string{advance().text};
        } else if (match(TokenKind::LParen)) {
            std::string raw;
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) raw += advance().text;
            expect(TokenKind::RParen, "expected ')'");
            config = std::move(raw);
        }
        return {std::move(ns), std::move(op), std::move(config), loc};
    }

    // 'type' is a keyword token but also a valid @base stage op.
    if (check(TokenKind::KwType)) {
        advance();
        auto config = parse_config(IrshBackend::ConfigKind::TypeName, loc);
        return {"base", "type", std::move(config), loc};
    }

    if (!check(TokenKind::Ident)) {
        emit_error("expected stage name");
        advance();
        return {"base", "filter", Expr{BoolLit{true, {}}}, loc};
    }

    auto sv = peek().text;

    // Bare-word: look up in auto-import table
    for (auto& entry : imports_) {
        if (!entry.as_stage || entry.op != sv) continue;
        advance();
        BackendConfig config = parse_config(entry.config, loc);
        return {entry.ns, entry.op, std::move(config), loc};
    }

    // Unknown bare word in stage position → external command (pipe stdin/stdout).
    // Mirrors multi-word exec sugar in source position: `grep -n "foo"`, `awk '{print $1}'`.
    {
        std::string cmd{sv};
        advance();

        auto is_arg = [](TokenKind k) {
            switch (k) {
            case TokenKind::Ident:
            case TokenKind::String:
            case TokenKind::Integer:
            case TokenKind::Float:
            case TokenKind::Bool:
            case TokenKind::FlagStr:
            case TokenKind::PathLiteral:
            case TokenKind::Dollar:
                return true;
            default:
                return false;
            }
        };

        ExecArgs args;
        std::string_view last_sv = sv;
        args.push_back({false, std::move(cmd)});
        while (is_arg(peek().kind)) {
            if (peek().kind == TokenKind::Dollar) {
                last_sv = {};
                advance();
                std::string name;
                if (check(TokenKind::Ident)) name = std::string{advance().text};
                args.push_back({true, std::move(name)});
            } else {
                const Token& tok = peek();
                if (tok.kind == TokenKind::PathLiteral && !args.empty() &&
                    !args.back().is_var && adjacent(last_sv, tok.text)) {
                    last_sv = tok.text;
                    args.back().text += std::string{advance().text};
                } else {
                    last_sv = tok.text;
                    advance();
                    std::string text{tok.text};
                    if (tok.kind == TokenKind::String && text.size() >= 2 &&
                        text.front() == '"' && text.back() == '"')
                        text = text.substr(1, text.size() - 2);
                    args.push_back({false, std::move(text)});
                }
            }
        }
        return {"os", "exec", std::move(args), loc};
    }
}

// Parse exec( words $var ... ) — safe argv without shell interpolation
ExecArgs Parser::parse_exec_args() {
    ExecArgs result;
    if (!match(TokenKind::LParen)) return result;
    while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
        if (check(TokenKind::Comma)) { advance(); continue; }  // tolerate comma-separated form
        if (match(TokenKind::Dollar)) {
            std::string name;
            if (check(TokenKind::Ident)) name = std::string{advance().text};
            result.push_back({true, std::move(name)});
        } else {
            auto& tok = advance();
            std::string text{tok.text};
            if (tok.kind == TokenKind::String && text.size() >= 2 &&
                text.front() == '"' && text.back() == '"')
                text = text.substr(1, text.size() - 2);
            result.push_back({false, std::move(text)});
        }
    }
    expect(TokenKind::RParen, "expected ')'");
    return result;
}

// Parse config for an op based on its declared ConfigKind.
// Called after the op name has been consumed.
BackendConfig Parser::parse_config(IrshBackend::ConfigKind kind, Loc /*loc*/) {
    using CK = IrshBackend::ConfigKind;
    switch (kind) {
    case CK::Expr:
        return Expr{parse_expr()};

    case CK::SortArg: {
        if (check(TokenKind::KwBy)) { advance(); expect(TokenKind::Colon, "expected ':' after 'by'"); }
        auto& f   = expect(TokenKind::Ident, "expected sort field name");
        bool desc = (check(TokenKind::Ident) && peek().text == "desc") ? (advance(), true) : false;
        return SortArg{std::string{f.text}, desc};
    }

    case CK::FieldList: {
        expect(TokenKind::LBrace, "expected '{'");
        std::vector<std::string> fields;
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            fields.push_back(std::string{peek().text}); advance();
            if (!match(TokenKind::Comma)) break;
        }
        expect(TokenKind::RBrace, "expected '}'");
        return fields;
    }

    case CK::IntExpr: {
        if (check(TokenKind::Dollar)) return parse_primary();
        int64_t n = 10;
        if (check(TokenKind::Integer)) {
            n = std::strtoll(std::string{advance().text}.c_str(), nullptr, 10);
        } else if (match(TokenKind::LParen)) {
            if (check(TokenKind::Dollar)) { auto e = parse_primary(); expect(TokenKind::RParen, "expected ')'"); return e; }
            n = std::strtoll(std::string{expect(TokenKind::Integer, "expected integer").text}.c_str(), nullptr, 10);
            expect(TokenKind::RParen, "expected ')'");
        }
        return Expr{IntLit{n, {}}};
    }

    case CK::String: {
        std::string path;
        auto consume_str = [&]() {
            if (check(TokenKind::String)) {
                auto raw = advance().text;
                path = (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
                       ? std::string{raw.substr(1, raw.size() - 2)} : std::string{raw};
                return true;
            }
            if (check(TokenKind::PathLiteral)) {
                path = std::string{advance().text};
                return true;
            }
            return false;
        };
        if (!consume_str() && match(TokenKind::LParen)) {
            consume_str();
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) advance();
            expect(TokenKind::RParen, "expected ')'");
        }
        return std::string{std::move(path)};
    }

    case CK::LsArgs: {
        std::string flags, path;
        if (check(TokenKind::FlagStr)) flags = std::string{advance().text};
        if (check(TokenKind::String) || check(TokenKind::PathLiteral)) {
            auto raw = advance().text;
            if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
                path = std::string{raw.substr(1, raw.size() - 2)};
            else
                path = std::string{raw};
        } else if (check(TokenKind::LParen)) {
            advance();
            std::string raw;
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) raw += advance().text;
            expect(TokenKind::RParen, "expected ')'");
            path = std::move(raw);
        }
        if (!flags.empty() || !path.empty())
            return flags + (flags.empty() || path.empty() ? "" : " ") + path;
        return std::monostate{};
    }

    case CK::TypeName: {
        if (match(TokenKind::LParen)) {
            std::string raw;
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) raw += advance().text;
            expect(TokenKind::RParen, "expected ')'");
            return raw;
        }
        if (check(TokenKind::Ident)) return std::string{advance().text};
        return std::monostate{};
    }

    case CK::Lit: {
        if (!match(TokenKind::LParen)) return std::monostate{};
        BackendConfig cfg;
        if (check(TokenKind::Dollar)) {
            cfg = Expr{parse_primary()};
        } else if (check(TokenKind::String)) {
            auto raw = advance().text;
            cfg = (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
                  ? std::string{raw.substr(1, raw.size() - 2)} : std::string{raw};
        } else if (check(TokenKind::Integer) || check(TokenKind::Float)) {
            cfg = std::string{advance().text};
        }
        expect(TokenKind::RParen, "expected ')'");
        return cfg;
    }

    case CK::ExecArgs:
        if (check(TokenKind::LParen)) return parse_exec_args();
        if (check(TokenKind::String) || check(TokenKind::PathLiteral))
            return std::string{advance().text};
        return std::monostate{};

    case CK::None:
    default:
        return std::monostate{};
    }
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
    if (match(TokenKind::Dollar)) {
        std::string name;
        if (check(TokenKind::Ident)) name = std::string{advance().text};
        DollarExpr de{name, std::monostate{}, loc};
        if (match(TokenKind::LBracket)) {
            // $args[N]
            if (check(TokenKind::Integer)) {
                int64_t idx = std::strtoll(std::string{advance().text}.c_str(), nullptr, 10);
                de.access = idx;
            }
            expect(TokenKind::RBracket, "expected ']'");
        } else if (check(TokenKind::Dot)) {
            // $args.flag
            advance(); // consume '.'
            if (check(TokenKind::Ident)) {
                de.access = std::string{advance().text};
            }
        }
        return de;
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
