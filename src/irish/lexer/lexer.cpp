/// @file src/irish/lexer/lexer.cpp
#include "lexer.hpp"

namespace iris::irsh {

Lexer::Lexer(std::string_view src) : src_(src) {}

// ── TODO: implement ───────────────────────────────────────────────────────────

std::vector<Token> Lexer::tokenise() { return {}; }

char Lexer::peek(size_t ahead) const {
    return (pos_ + ahead < src_.size()) ? src_[pos_ + ahead] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; } else { ++col_; }
    return c;
}

void  Lexer::skip_whitespace_and_comments() {}
Token Lexer::make(TokenKind k, size_t start, uint32_t l, uint32_t c) {
    return { k, src_.substr(start, pos_ - start), l, c };
}
Token Lexer::lex_string()                                          { return {}; }
Token Lexer::lex_number(size_t, uint32_t, uint32_t)               { return {}; }
Token Lexer::lex_ident_or_keyword(size_t, uint32_t, uint32_t)     { return {}; }
Token Lexer::lex_path(size_t, uint32_t, uint32_t)                 { return {}; }

} // namespace iris::irsh
