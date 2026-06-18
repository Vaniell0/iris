#include <gtest/gtest.h>
#include "irish/lexer/lexer.hpp"

using namespace iris::irsh;

static std::vector<Token> lex(std::string_view src) {
    return Lexer{src}.tokenise();
}

static std::vector<TokenKind> kinds(const std::vector<Token>& toks) {
    std::vector<TokenKind> out;
    for (auto& t : toks)
        if (t.kind != TokenKind::Eof) out.push_back(t.kind);
    return out;
}

TEST(Lexer, EmptyInput) {
    auto toks = lex("");
    ASSERT_EQ(toks.size(), 1u);
    EXPECT_EQ(toks[0].kind, TokenKind::Eof);
}

TEST(Lexer, Keywords) {
    auto toks = lex("let type by true false");
    ASSERT_EQ(kinds(toks), (std::vector<TokenKind>{
        TokenKind::KwLet, TokenKind::KwType, TokenKind::KwBy,
        TokenKind::Bool,  TokenKind::Bool,
    }));
    EXPECT_EQ(toks[3].text, "true");
    EXPECT_EQ(toks[4].text, "false");
}

TEST(Lexer, Integer) {
    auto toks = lex("42 -7 0");
    EXPECT_EQ(toks[0].kind, TokenKind::Integer); EXPECT_EQ(toks[0].text, "42");
    EXPECT_EQ(toks[1].kind, TokenKind::Integer); EXPECT_EQ(toks[1].text, "-7");
    EXPECT_EQ(toks[2].kind, TokenKind::Integer); EXPECT_EQ(toks[2].text, "0");
}

TEST(Lexer, Float) {
    auto toks = lex("3.14 -0.5");
    EXPECT_EQ(toks[0].kind, TokenKind::Float); EXPECT_EQ(toks[0].text, "3.14");
    EXPECT_EQ(toks[1].kind, TokenKind::Float); EXPECT_EQ(toks[1].text, "-0.5");
}

TEST(Lexer, NegativeNotFloat) {
    // "-" followed by non-digit is not a number
    auto toks = lex("a-b");
    EXPECT_EQ(toks[0].kind, TokenKind::Ident);
}

TEST(Lexer, StringWithEscape) {
    auto toks = lex(R"("hello \"world\"")");
    ASSERT_EQ(toks[0].kind, TokenKind::String);
    EXPECT_EQ(toks[0].text, R"("hello \"world\"")");
}

TEST(Lexer, PathLiterals) {
    auto toks = lex("/tmp ./foo ~/bar");
    EXPECT_EQ(toks[0].kind, TokenKind::PathLiteral); EXPECT_EQ(toks[0].text, "/tmp");
    EXPECT_EQ(toks[1].kind, TokenKind::PathLiteral); EXPECT_EQ(toks[1].text, "./foo");
    EXPECT_EQ(toks[2].kind, TokenKind::PathLiteral); EXPECT_EQ(toks[2].text, "~/bar");
}

TEST(Lexer, PathStopsAtPipe) {
    auto toks = lex("/tmp|head");
    EXPECT_EQ(toks[0].kind, TokenKind::PathLiteral); EXPECT_EQ(toks[0].text, "/tmp");
    EXPECT_EQ(toks[1].kind, TokenKind::Pipe);
}

TEST(Lexer, Operators) {
    auto toks = lex("| || & && &! ?? ?| == != < <= > >= !");
    auto ks = kinds(toks);
    ASSERT_EQ(ks.size(), 14u);
    EXPECT_EQ(ks[0],  TokenKind::Pipe);
    EXPECT_EQ(ks[1],  TokenKind::OrOr);
    EXPECT_EQ(ks[2],  TokenKind::ParallelPipe);
    EXPECT_EQ(ks[3],  TokenKind::AndAnd);
    EXPECT_EQ(ks[4],  TokenKind::FireForget);
    EXPECT_EQ(ks[5],  TokenKind::FallbackVal);
    EXPECT_EQ(ks[6],  TokenKind::FallbackPipe);
    EXPECT_EQ(ks[7],  TokenKind::EqEq);
    EXPECT_EQ(ks[8],  TokenKind::NotEq);
    EXPECT_EQ(ks[9],  TokenKind::Lt);
    EXPECT_EQ(ks[10], TokenKind::LtEq);
    EXPECT_EQ(ks[11], TokenKind::Gt);
    EXPECT_EQ(ks[12], TokenKind::GtEq);
    EXPECT_EQ(ks[13], TokenKind::Bang);
}

TEST(Lexer, Punctuation) {
    auto toks = lex("@ . : , = ( ) { }");
    auto ks = kinds(toks);
    EXPECT_EQ(ks[0], TokenKind::At);
    EXPECT_EQ(ks[1], TokenKind::Dot);
    EXPECT_EQ(ks[2], TokenKind::Colon);
    EXPECT_EQ(ks[3], TokenKind::Comma);
    EXPECT_EQ(ks[4], TokenKind::Assign);
    EXPECT_EQ(ks[5], TokenKind::LParen);
    EXPECT_EQ(ks[6], TokenKind::RParen);
    EXPECT_EQ(ks[7], TokenKind::LBrace);
    EXPECT_EQ(ks[8], TokenKind::RBrace);
}

TEST(Lexer, Comments) {
    auto toks = lex("let x = 1 # ignored\nlet y = 2");
    auto ks = kinds(toks);
    // let x = 1 let y = 2
    ASSERT_EQ(ks.size(), 8u);
    EXPECT_EQ(ks[0], TokenKind::KwLet);
    EXPECT_EQ(ks[4], TokenKind::KwLet);
}

TEST(Lexer, LineColTracking) {
    auto toks = lex("let\nx");
    EXPECT_EQ(toks[0].line, 1u); EXPECT_EQ(toks[0].col, 1u);
    EXPECT_EQ(toks[1].line, 2u); EXPECT_EQ(toks[1].col, 1u);
}

TEST(Lexer, FullPipeline) {
    auto toks = lex(R"(let x = @os.ls("/tmp") | filter(name == "foo") | head 5)");
    auto ks = kinds(toks);
    //  let x = @ os . ls ( "/tmp" ) | filter ( name == "foo" ) | head 5
    EXPECT_EQ(ks[0],  TokenKind::KwLet);
    EXPECT_EQ(ks[1],  TokenKind::Ident);    // x
    EXPECT_EQ(ks[2],  TokenKind::Assign);
    EXPECT_EQ(ks[3],  TokenKind::At);
    EXPECT_EQ(ks[4],  TokenKind::Ident);    // os
    EXPECT_EQ(ks[5],  TokenKind::Dot);
    EXPECT_EQ(ks[6],  TokenKind::Ident);    // ls
    EXPECT_EQ(ks[7],  TokenKind::LParen);
    EXPECT_EQ(ks[8],  TokenKind::String);
    EXPECT_EQ(ks[9],  TokenKind::RParen);
    EXPECT_EQ(ks[10], TokenKind::Pipe);
    EXPECT_EQ(ks[11], TokenKind::Ident);    // filter
    EXPECT_EQ(ks[17], TokenKind::Pipe);
    EXPECT_EQ(ks[18], TokenKind::Ident);    // head
    EXPECT_EQ(ks[19], TokenKind::Integer);  // 5
}
