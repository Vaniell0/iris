/// @file src/irish/main.cpp
/// Entry point — three-mode detection, then dispatch.
#include "session/session.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "checker/checker.hpp"
#include "exec/executor.hpp"
#include <registry.hpp>
#include <unistd.h>
#include <cstdio>
#include <fstream>
#include <sstream>

static int run_script(const char* path, iris::irsh::Session& session);
static int run_repl(iris::irsh::Session& session);
static int run_pipeline_component(iris::irsh::Session& session);

int main(int argc, char** argv) {
    // Freeze global registry before any irsh statement runs.
    iris::TypeRegistry::global().freeze();

    iris::irsh::Session session;

    if (argc > 1) {
        return run_script(argv[1], session);
    }

    if (isatty(STDIN_FILENO)) {
        return run_repl(session);
    }

    // stdin is a pipe — pipeline-component mode
    return run_pipeline_component(session);
}

// ── TODO: implement ───────────────────────────────────────────────────────────

static int run_script(const char*, iris::irsh::Session&) {
    std::fprintf(stderr, "irish: script mode not yet implemented\n");
    return 2;
}

static const char* token_kind_name(iris::irsh::TokenKind k) {
    using K = iris::irsh::TokenKind;
    switch (k) {
        case K::Ident:        return "Ident";
        case K::Integer:      return "Integer";
        case K::Float:        return "Float";
        case K::String:       return "String";
        case K::Bool:         return "Bool";
        case K::PathLiteral:  return "PathLiteral";
        case K::Pipe:         return "Pipe";
        case K::ParallelPipe: return "ParallelPipe";
        case K::FireForget:   return "FireForget";
        case K::FallbackVal:  return "FallbackVal";
        case K::FallbackPipe: return "FallbackPipe";
        case K::OrOr:         return "OrOr";
        case K::AndAnd:       return "AndAnd";
        case K::At:           return "At";
        case K::Dot:          return "Dot";
        case K::Colon:        return "Colon";
        case K::Comma:        return "Comma";
        case K::Assign:       return "Assign";
        case K::EqEq:         return "EqEq";
        case K::NotEq:        return "NotEq";
        case K::Lt:           return "Lt";
        case K::LtEq:         return "LtEq";
        case K::Gt:           return "Gt";
        case K::GtEq:         return "GtEq";
        case K::Bang:         return "Bang";
        case K::LParen:       return "LParen";
        case K::RParen:       return "RParen";
        case K::LBrace:       return "LBrace";
        case K::RBrace:       return "RBrace";
        case K::KwLet:        return "KwLet";
        case K::KwType:       return "KwType";
        case K::KwBy:         return "KwBy";
        case K::Eof:          return "Eof";
        case K::Error:        return "Error";
    }
    return "?";
}

static int run_repl(iris::irsh::Session&) {
    char buf[4096];
    while (true) {
        std::fputs(">> ", stdout);
        std::fflush(stdout);
        if (!std::fgets(buf, sizeof(buf), stdin)) break;

        std::string_view line{buf};
        if (!line.empty() && line.back() == '\n') line.remove_suffix(1);
        if (line == "exit" || line == "quit") break;
        if (line.empty()) continue;

        iris::irsh::Lexer lexer{line};
        auto tokens = lexer.tokenise();
        for (auto& t : tokens) {
            if (t.kind == iris::irsh::TokenKind::Eof) break;
            std::printf("  %-14s  %.*s\n",
                token_kind_name(t.kind),
                static_cast<int>(t.text.size()), t.text.data());
        }
    }
    return 0;
}

static int run_pipeline_component(iris::irsh::Session&) {
    std::fprintf(stderr, "irish: pipeline-component mode not yet implemented\n");
    return 2;
}
