/// @file src/irish/lexer/lexer.cpp
#include "lexer.hpp"
#include <cctype>

namespace iris::irsh {

Lexer::Lexer(std::string_view src) : src_(src) {}

// ── Helpers ───────────────────────────────────────────────────────────────────

char Lexer::peek(size_t ahead) const {
    return (pos_ + ahead < src_.size()) ? src_[pos_ + ahead] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; } else { ++col_; }
    return c;
}

Token Lexer::make(TokenKind k, size_t start, uint32_t l, uint32_t c) {
    return { k, src_.substr(start, pos_ - start), l, c };
}

void Lexer::skip_whitespace_and_comments() {
    while (pos_ < src_.size()) {
        if (std::isspace(peek())) { advance(); continue; }
        if (peek() == '#') { while (pos_ < src_.size() && peek() != '\n') advance(); continue; }
        break;
    }
}

// ── String literal — "..." with \" escape ─────────────────────────────────────

Token Lexer::lex_string() {
    uint32_t l = line_, c = col_;
    size_t start = pos_;
    advance(); // opening "
    while (pos_ < src_.size() && peek() != '"') {
        if (peek() == '\\') advance(); // skip escape
        advance();
    }
    if (peek() == '"') advance(); // closing "
    return make(TokenKind::String, start, l, c);
}

// ── Number ───────────────────────────────────────────────────────────────────

Token Lexer::lex_number(size_t start, uint32_t l, uint32_t c) {
    if (peek() == '-') advance();
    while (std::isdigit(peek())) advance();
    if (peek() == '.' && std::isdigit(peek(1))) {
        advance();
        while (std::isdigit(peek())) advance();
        return make(TokenKind::Float, start, l, c);
    }
    return make(TokenKind::Integer, start, l, c);
}

// ── Identifier or keyword ─────────────────────────────────────────────────────

Token Lexer::lex_ident_or_keyword(size_t start, uint32_t l, uint32_t c) {
    while (std::isalnum(peek()) || peek() == '_' || peek() == '-') advance();
    auto text = src_.substr(start, pos_ - start);
    TokenKind k = TokenKind::Ident;
    if (text == "let")   k = TokenKind::KwLet;
    if (text == "type")  k = TokenKind::KwType;
    if (text == "by")    k = TokenKind::KwBy;
    if (text == "true" || text == "false") k = TokenKind::Bool;
    return { k, text, l, c };
}

// ── Path literal — starts with / ./ ~/ ───────────────────────────────────────

Token Lexer::lex_path(size_t start, uint32_t l, uint32_t c) {
    while (pos_ < src_.size() && !std::isspace(peek()) && peek() != '|' && peek() != ')') advance();
    return make(TokenKind::PathLiteral, start, l, c);
}

// ── Main tokenise loop ────────────────────────────────────────────────────────

std::vector<Token> Lexer::tokenise() {
    std::vector<Token> out;
    while (true) {
        skip_whitespace_and_comments();
        if (pos_ >= src_.size()) break;

        uint32_t l = line_, c = col_;
        size_t   start = pos_;
        char     ch = peek();

        // String literal
        if (ch == '"') { out.push_back(lex_string()); continue; }

        // Number (including negative)
        if (std::isdigit(ch) || (ch == '-' && std::isdigit(peek(1))))
            { out.push_back(lex_number(start, l, c)); continue; }

        // Flag string: -a -la -St (dash followed by letters only)
        if (ch == '-' && std::isalpha(peek(1))) {
            advance(); // consume '-'
            while (std::isalpha(peek())) advance();
            out.push_back(make(TokenKind::FlagStr, start, l, c));
            continue;
        }

        // Path literals: /abs, ./rel, ~/home, .. parent, ../rel
        if (ch == '/' ||
            (ch == '~' && peek(1) == '/') ||
            (ch == '.' && peek(1) == '/') ||
            (ch == '.' && peek(1) == '.'))
            { out.push_back(lex_path(start, l, c)); continue; }

        // Identifiers and keywords
        if (std::isalpha(ch) || ch == '_')
            { out.push_back(lex_ident_or_keyword(start, l, c)); continue; }

        advance(); // consume the character before matching

        switch (ch) {
            case '|':
                if (peek() == '|') { advance(); out.push_back(make(TokenKind::OrOr,        start, l, c)); }
                else                             out.push_back(make(TokenKind::Pipe,         start, l, c));
                break;
            case '&':
                if (peek() == '!')  { advance(); out.push_back(make(TokenKind::FireForget,  start, l, c)); }
                else if (peek()=='&'){ advance(); out.push_back(make(TokenKind::AndAnd,      start, l, c)); }
                else                             out.push_back(make(TokenKind::ParallelPipe, start, l, c));
                break;
            case '?':
                if (peek() == '?')  { advance(); out.push_back(make(TokenKind::FallbackVal,  start, l, c)); }
                else if (peek()=='|'){ advance(); out.push_back(make(TokenKind::FallbackPipe, start, l, c)); }
                else                             out.push_back(make(TokenKind::Error,         start, l, c));
                break;
            case '=':
                if (peek() == '=')  { advance(); out.push_back(make(TokenKind::EqEq,   start, l, c)); }
                else                             out.push_back(make(TokenKind::Assign,  start, l, c));
                break;
            case '!':
                if (peek() == '=')  { advance(); out.push_back(make(TokenKind::NotEq,  start, l, c)); }
                else                             out.push_back(make(TokenKind::Bang,    start, l, c));
                break;
            case '<':
                if (peek() == '=')  { advance(); out.push_back(make(TokenKind::LtEq,   start, l, c)); }
                else                             out.push_back(make(TokenKind::Lt,      start, l, c));
                break;
            case '>':
                if (peek() == '=')  { advance(); out.push_back(make(TokenKind::GtEq,   start, l, c)); }
                else                             out.push_back(make(TokenKind::Gt,      start, l, c));
                break;
            case '@': out.push_back(make(TokenKind::At,         start, l, c)); break;
            case '.': out.push_back(make(TokenKind::Dot,        start, l, c)); break;
            case ':': out.push_back(make(TokenKind::Colon,      start, l, c)); break;
            case ',': out.push_back(make(TokenKind::Comma,      start, l, c)); break;
            case '(': out.push_back(make(TokenKind::LParen,     start, l, c)); break;
            case ')': out.push_back(make(TokenKind::RParen,     start, l, c)); break;
            case '{': out.push_back(make(TokenKind::LBrace,     start, l, c)); break;
            case '}': out.push_back(make(TokenKind::RBrace,     start, l, c)); break;
            default:  out.push_back(make(TokenKind::Error,      start, l, c)); break;
        }
    }
    out.push_back({ TokenKind::Eof, {}, line_, col_ });
    return out;
}

} // namespace iris::irsh
