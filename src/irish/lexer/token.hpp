#pragma once
#include <cstdint>
#include <string_view>

namespace iris::irsh {

enum class TokenKind : uint8_t {
    // Literals
    Ident,          // foo, ls, filter, MyType
    Integer,        // 42, -7
    Float,          // 3.14
    String,         // "hello" or bareword-string (same token kind)
    Bool,           // true, false
    PathLiteral,    // starts with / ./ ~/ — sugar for @os.exec in pipeline pos

    // Punctuation
    Pipe,           // |
    ParallelPipe,   // &
    FireForget,     // &!
    FallbackVal,    // ??
    FallbackPipe,   // ?|
    At,             // @
    Dot,            // .
    Colon,          // :
    Comma,          // ,
    Assign,         // =
    LParen,         // (
    RParen,         // )
    LBrace,         // {
    RBrace,         // }

    // Comparison
    EqEq,           // ==
    NotEq,          // !=
    Lt,             // <
    LtEq,           // <=
    Gt,             // >
    GtEq,           // >=

    // Logical
    AndAnd,         // &&
    OrOr,           // ||
    Bang,           // !

    FlagStr,        // -a -la -St  (ls flag shorthand)

    // Keywords
    KwLet,          // let
    KwType,         // type
    KwBy,           // by   (sort by: field)

    Eof,
    Error,
};

struct Token {
    TokenKind        kind;
    std::string_view text;  // slice into source — zero allocation
    uint32_t         line;
    uint32_t         col;
};

} // namespace iris::irsh
