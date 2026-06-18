#pragma once
#include "token.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace iris::irsh {

struct LexError {
    uint32_t    line;
    uint32_t    col;
    std::string msg;
};

class Lexer {
public:
    explicit Lexer(std::string_view src);

    // Tokenise the full source. Returns all tokens including Eof.
    // On lex error the offending token has kind=Error; lexing continues.
    std::vector<Token> tokenise();

private:
    std::string_view src_;
    size_t           pos_  = 0;
    uint32_t         line_ = 1;
    uint32_t         col_  = 1;

    char        peek(size_t ahead = 0) const;
    char        advance();
    void        skip_whitespace_and_comments();
    Token       make(TokenKind, size_t start, uint32_t line, uint32_t col);
    Token       lex_string();
    Token       lex_number(size_t start, uint32_t line, uint32_t col);
    Token       lex_ident_or_keyword(size_t start, uint32_t line, uint32_t col);
    Token       lex_path(size_t start, uint32_t line, uint32_t col);
};

} // namespace iris::irsh
