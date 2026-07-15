/// @file src/irish/repl/eval.cpp
#include "eval.hpp"
#include "../lexer/lexer.hpp"
#include "../parser/parser.hpp"
#include "../parser/import_table.hpp"
#include <registry.hpp>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace iris::irsh {

static const char* kind_name(iris::PrimitiveKind k) {
    using K = iris::PrimitiveKind;
    switch (k) {
        case K::Void:  return "void";
        case K::Bool:  return "bool";
        case K::I8:    return "i8";   case K::I16:  return "i16";
        case K::I32:   return "i32";  case K::I64:  return "i64";
        case K::F32:   return "f32";  case K::F64:  return "f64";
        case K::Str:   return "str";  case K::CStr: return "cstr";
        case K::Bytes: return "bytes";
    }
    return "?";
}

static const char* token_kind_name(TokenKind k) {
    using K = TokenKind;
    switch (k) {
        case K::Ident:        return "Ident";
        case K::Integer:      return "Integer";
        case K::Float:        return "Float";
        case K::String:       return "String";
        case K::Bool:         return "Bool";
        case K::PathLiteral:  return "PathLiteral";
        case K::FlagStr:      return "FlagStr";
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
        case K::KwImport:     return "KwImport";
        case K::Dollar:       return "Dollar";
        case K::LBracket:     return "LBracket";
        case K::RBracket:     return "RBracket";
        case K::Semi:         return "Semi";
        case K::Eof:          return "Eof";
        case K::Error:        return "Error";
    }
    return "?";
}

int repl_eval(const std::string& input,
              Session& session,
              Checker& checker,
              Executor& exec,
              BackendRegistry& registry) {
    // cd — built-in: changes process cwd; not an irsh pipeline
    if (input == "cd" || input.starts_with("cd ")) {
        std::string path = input.size() > 3 ? input.substr(3) : std::string{};
        while (!path.empty() && path.front() == ' ') path.erase(path.begin());
        while (!path.empty() && path.back()  == ' ') path.pop_back();
        if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
            path = path.substr(1, path.size() - 2);
        if (path.empty() || path == "~")
            if (auto* h = std::getenv("HOME")) path = h;
        if (::chdir(path.c_str()) != 0) {
            std::fprintf(stderr, "cd: %s: %s\n", path.c_str(), std::strerror(errno));
            return 1;
        }
        return 0;
    }
    if (input == ":types") {
        auto& reg = iris::TypeRegistry::global();
        for (auto& [id, d] : reg.all()) {
            std::printf("struct %s {\n", d.name.c_str());
            for (auto& f : d.fields)
                std::printf("    %-12s : %-6s  offset=%-4zu  size=%zu\n",
                    f.name.c_str(), kind_name(f.kind), f.offset, f.size);
            std::printf("}\n");
        }
        return 0;
    }
    if (input.starts_with(":type ")) {
        std::string tname = input.substr(6);
        while (!tname.empty() && tname.back() == ' ') tname.pop_back();
        auto& reg = iris::TypeRegistry::global();
        if (auto* d = reg.find(tname)) {
            std::printf("struct %s {\n", d->name.c_str());
            for (auto& f : d->fields)
                std::printf("    %-12s : %-6s  offset=%-4zu  size=%zu\n",
                    f.name.c_str(), kind_name(f.kind), f.offset, f.size);
            std::printf("}\n");
        } else {
            std::fprintf(stderr, "type '%s' not found\n", tname.c_str());
        }
        return 0;
    }
    if (input.starts_with(":lex ")) {
        Lexer lexer{std::string_view{input}.substr(5)};
        for (auto& t : lexer.tokenise()) {
            if (t.kind == TokenKind::Eof) break;
            std::printf("  %-14s  %.*s\n",
                token_kind_name(t.kind),
                static_cast<int>(t.text.size()), t.text.data());
        }
        return 0;
    }

    // Import table — rebuilt each call so it reflects the current session.imports().
    auto itbl = make_import_table(registry, session);

    // Variable reference: "x" or "x | head 3"
    {
        auto pipe_pos = input.find('|');
        std::string name = pipe_pos == std::string::npos
            ? input : input.substr(0, pipe_pos);
        while (!name.empty() && name.back() == ' ') name.pop_back();

        if (session.get_materialized(name)) {
            // Materialized variable — build a fake _var pipeline and run it
            std::string fake = name;
            if (pipe_pos != std::string::npos) fake += input.substr(pipe_pos);
            Lexer flx{fake};
            Parser fpx{flx.tokenise(), itbl};
            auto fr = fpx.parse();
            int rc = 0;
            if (fr.ok() && !fr.program.stmts.empty()) {
                auto typed = checker.check(fr.program);
                if (typed.ok())
                    for (auto& stmt : typed.stmts) {
                        auto res = exec.run_stmt(stmt);
                        if (!res) {
                            auto& e = res.error();
                            if (e.loc.line)
                                std::fprintf(stderr, "runtime %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
                            else
                                std::fprintf(stderr, "runtime: %s\n", e.msg.c_str());
                            rc = 1;
                        }
                    }
                else for (auto& e : typed.errors)
                    std::fprintf(stderr, "type %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
            }
            return rc;
        }
        if (auto* tp = session.get_pipeline(name)) {
            TypedPipeline composed = *tp;
            if (pipe_pos != std::string::npos) {
                // Parse extra stages appended after '|' using a dummy source.
                std::string fake = "@_var | " + input.substr(pipe_pos + 1);
                Lexer flx{fake};
                Parser fpx{flx.tokenise(), itbl};
                auto fr = fpx.parse();
                if (fr.ok() && !fr.program.stmts.empty()) {
                    if (auto* fp = std::get_if<Pipeline>(&fr.program.stmts[0])) {
                        IrType cur = composed.stages.empty()
                            ? composed.source_type
                            : composed.stages.back().out_type;
                        for (auto& stage : fp->stages) {
                            IrType out = checker.check_stage(stage, cur);
                            composed.stages.push_back({stage, out});
                            cur = out;
                        }
                    }
                }
            }
            auto res = exec.run(composed);
            if (!res) {
                auto& e = res.error();
                if (e.loc.line)
                    std::fprintf(stderr, "runtime %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
                else
                    std::fprintf(stderr, "runtime: %s\n", e.msg.c_str());
            }
            return res ? 0 : 1;
        }
    }

    // Normal parse → check → execute
    Lexer lexer{input};
    auto tokens = lexer.tokenise();
    Parser parser{std::move(tokens), itbl};
    auto parse_result = parser.parse();

    if (!parse_result.ok()) {
        for (auto& e : parse_result.errors)
            std::fprintf(stderr, "parse %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
        return 2;
    }

    auto typed = checker.check(parse_result.program);
    if (!typed.ok()) {
        for (auto& e : typed.errors)
            std::fprintf(stderr, "type %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
        return 2;
    }

    int rc = 0;
    for (auto& stmt : typed.stmts) {
        auto res = exec.run_stmt(stmt);
        if (!res) {
            auto& e = res.error();
            if (e.loc.line)
                std::fprintf(stderr, "runtime %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
            else
                std::fprintf(stderr, "runtime: %s\n", e.msg.c_str());
            rc = 1;
        }
    }
    return rc;
}

} // namespace iris::irsh
