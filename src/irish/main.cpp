/// @file src/irish/main.cpp
/// Entry point — three-mode detection, then dispatch.
#include "session/session.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "parser/import_table.hpp"
#include "checker/checker.hpp"
#include "exec/executor.hpp"
#include "backend/os_irsh.hpp"
#include "backend/base_irsh.hpp"
#include "backend/ipc_irsh.hpp"
#include "backend/plugin_loader.hpp"
#include <registry.hpp>
#include <filesystem>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_set>
#ifdef IRIS_HAS_REPLXX
#  include <replxx.hxx>
#endif

static iris::irsh::BackendRegistry g_registry;

static int run_script(const char* path, iris::irsh::Session& session);
static int run_repl(iris::irsh::Session& session);
static int run_pipeline_component(iris::irsh::Session& session);
static int repl_eval(const std::string& input, iris::irsh::Session& session,
                     iris::irsh::Checker& checker, iris::irsh::Executor& exec);

int main(int argc, char** argv) {
    // Session must be created before BaseIrshBackend so its registry reference is stable
    iris::irsh::Session session;

    // Register built-in backends (BaseIrshBackend holds TypeRegistry refs — must outlive registry)
    g_registry.register_backend(std::make_unique<iris::irsh::BaseIrshBackend>(
        iris::TypeRegistry::global(), session));
    g_registry.register_backend(std::make_unique<iris::irsh::OsIrshBackend>());
    g_registry.register_backend(std::make_unique<iris::irsh::IpcIrshBackend>());

    // Load plugins from ~/.iris/plugins/*.so (non-fatal: warn and continue)
    for (auto& err : iris::irsh::load_plugins(g_registry))
        std::fprintf(stderr, "irish: plugin warning: %s\n", err.c_str());

    g_registry.freeze();
    iris::TypeRegistry::global().freeze();

    // Load ~/.irshrc — default imports and user config (non-fatal)
    if (const char* home = std::getenv("HOME")) {
        std::string rc = std::string{home} + "/.irshrc";
        if (std::ifstream{rc}.good())
            run_script(rc.c_str(), session);
    }

    if (argc > 1 && std::string_view{argv[1]} == "--type-check" && argc > 2) {
        std::ifstream f{argv[2]};
        if (!f) { std::fprintf(stderr, "irish: cannot open '%s'\n", argv[2]); return 1; }
        iris::irsh::Checker checker{iris::TypeRegistry::global(), session.session_types(), g_registry};
        auto itbl = iris::irsh::make_import_table(g_registry, session);
        int rc = 0;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            iris::irsh::Lexer lx{line};
            iris::irsh::Parser px{lx.tokenise(), itbl};
            auto pr = px.parse();
            if (!pr.ok()) {
                for (auto& e : pr.errors)
                    std::fprintf(stderr, "parse %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
                rc = 2; continue;
            }
            auto typed = checker.check(pr.program);
            if (!typed.ok()) {
                for (auto& e : typed.errors)
                    std::fprintf(stderr, "type %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
                rc = 2;
            }
        }
        if (!rc) std::puts("OK");
        return rc;
    }
    if (argc > 1 && std::string_view{argv[1]} == "-e" && argc > 2) {
        iris::irsh::Checker  checker{iris::TypeRegistry::global(), session.session_types(), g_registry};
        iris::irsh::Executor exec{session, g_registry};
        return repl_eval(argv[2], session, checker, exec);
    }
    if (argc > 1) {
        std::vector<std::string> sargs;
        for (int i = 2; i < argc; ++i) sargs.push_back(argv[i]);
        session.set_script_args(std::move(sargs));
        return run_script(argv[1], session);
    }
    if (isatty(STDIN_FILENO)) return run_repl(session);
    return run_pipeline_component(session);
}

static int run_script(const char* path, iris::irsh::Session& session) {
    std::ifstream f{path};
    if (!f) { std::fprintf(stderr, "irish: cannot open '%s'\n", path); return 1; }
    iris::irsh::Checker  checker{iris::TypeRegistry::global(), session.session_types(), g_registry};
    iris::irsh::Executor exec{session, g_registry, iris::irsh::ExecMode::Script};
    std::string continuation, line;
    int rc = 0;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line.back() == '\\') {
            continuation += line.substr(0, line.size() - 1);
            continuation += ' ';
            continue;
        }
        std::string input = continuation + line;
        continuation.clear();
        if (input.empty() || input[0] == '#') continue;
        int r = repl_eval(input, session, checker, exec);
        if (r) rc = r;
    }
    if (!continuation.empty()) { int r = repl_eval(continuation, session, checker, exec); if (r) rc = r; }
    return rc;
}

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

// ── :lex debug command ────────────────────────────────────────────────────────

static const char* token_kind_name(iris::irsh::TokenKind k) {
    using K = iris::irsh::TokenKind;
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

// ── REPL eval ─────────────────────────────────────────────────────────────────

// Returns exit code: 0 success, 1 runtime error, 2 parse error, 3 backend unavailable.
static int repl_eval(const std::string& input,
                      iris::irsh::Session& session,
                      iris::irsh::Checker& checker,
                      iris::irsh::Executor& exec) {
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
        iris::irsh::Lexer lexer{std::string_view{input}.substr(5)};
        for (auto& t : lexer.tokenise()) {
            if (t.kind == iris::irsh::TokenKind::Eof) break;
            std::printf("  %-14s  %.*s\n",
                token_kind_name(t.kind),
                static_cast<int>(t.text.size()), t.text.data());
        }
        return 0;
    }

    // Import table — rebuilt each call so it reflects the current session.imports().
    auto itbl = iris::irsh::make_import_table(g_registry, session);

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
        iris::irsh::Lexer flx{fake};
        iris::irsh::Parser fpx{flx.tokenise(), itbl};
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
            iris::irsh::TypedPipeline composed = *tp;
            if (pipe_pos != std::string::npos) {
                // Parse extra stages appended after '|' using a dummy source.
                std::string fake = "@_var | " + input.substr(pipe_pos + 1);
                iris::irsh::Lexer flx{fake};
                iris::irsh::Parser fpx{flx.tokenise(), itbl};
                auto fr = fpx.parse();
                if (fr.ok() && !fr.program.stmts.empty()) {
                    if (auto* fp = std::get_if<iris::irsh::Pipeline>(&fr.program.stmts[0])) {
                        iris::irsh::IrType cur = composed.stages.empty()
                            ? composed.source_type
                            : composed.stages.back().out_type;
                        for (auto& stage : fp->stages) {
                            iris::irsh::IrType out = checker.check_stage(stage, cur);
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
    iris::irsh::Lexer lexer{input};
    auto tokens = lexer.tokenise();
    iris::irsh::Parser parser{std::move(tokens), itbl};
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

// ── REPL ──────────────────────────────────────────────────────────────────────

#ifdef IRIS_HAS_REPLXX

using Replxx   = replxx::Replxx;
using Color    = Replxx::Color;
using colors_t = Replxx::colors_t;

static Color token_color(iris::irsh::TokenKind k) {
    using K = iris::irsh::TokenKind;
    switch (k) {
        case K::KwLet: case K::KwType: case K::KwBy:
            return Color::BRIGHTMAGENTA;
        case K::At:
            return Color::BRIGHTGREEN;
        case K::Ident:
            return Color::DEFAULT;
        case K::String:
            return Color::YELLOW;
        case K::Integer: case K::Float:
            return Color::CYAN;
        case K::Bool:
            return Color::BRIGHTCYAN;
        case K::PathLiteral:
            return Color::BLUE;
        case K::FlagStr:
            return Color::BRIGHTBLUE;
        case K::Pipe: case K::ParallelPipe: case K::FireForget:
        case K::FallbackVal: case K::FallbackPipe:
        case K::OrOr: case K::AndAnd:
            return Color::BRIGHTRED;
        case K::EqEq: case K::NotEq: case K::Lt: case K::LtEq:
        case K::Gt: case K::GtEq: case K::Bang:
            return Color::WHITE;
        case K::Error:
            return Color::RED;
        default:
            return Color::DEFAULT;
    }
}

// UTF-8 byte offset → Unicode codepoint index (replxx colors_t is codepoint-indexed)
static size_t byte_to_cp(std::string_view s, size_t byte_off) {
    size_t cp = 0;
    for (size_t i = 0; i < byte_off && i < s.size(); ++cp) {
        unsigned char c = (unsigned char)s[i];
        if      (c < 0x80) i += 1;
        else if (c < 0xE0) i += 2;
        else if (c < 0xF0) i += 3;
        else               i += 4;
    }
    return cp;
}

static size_t cp_len(std::string_view s) { return byte_to_cp(s, s.size()); }

static void highlight(const std::string& input, colors_t& colors) {
    iris::irsh::Lexer lexer{input};
    for (auto& t : lexer.tokenise()) {
        if (t.kind == iris::irsh::TokenKind::Eof) break;
        size_t byte_pos = static_cast<size_t>(t.text.data() - input.data());
        size_t pos = byte_to_cp(input, byte_pos);
        size_t len = cp_len(t.text);
        Color c = token_color(t.kind);
        for (size_t i = 0; i < len && pos + i < colors.size(); ++i)
            colors[pos + i] = c;
    }
}

// Walk tokens before the first | to identify the source backend's element type.
// Used for tab completion and operator hints.
static const iris::TypeDescriptor* infer_source_desc(const std::string& input,
                                                      const iris::irsh::Session& session) {
    iris::irsh::Lexer l{input};
    auto toks = l.tokenise();

    // Parse source portion (up to first |), get BackendCall via Parser
    iris::irsh::Parser p{toks, iris::irsh::make_import_table(g_registry, session)};
    auto result = p.parse();
    if (result.program.stmts.empty()) return nullptr;

    // Extract the BackendCall for the source
    iris::irsh::BackendCall source;
    if (auto* pipe = std::get_if<iris::irsh::Pipeline>(&result.program.stmts[0]))
        source = pipe->source;
    else if (auto* let = std::get_if<iris::irsh::LetStmt>(&result.program.stmts[0]))
        source = let->rhs.source;
    else
        return nullptr;

    // Ask the backend what type it produces
    auto* backend = g_registry.find(source.ns);
    if (!backend) return nullptr;

    std::vector<iris::irsh::TypeError> errs;
    iris::irsh::IrType out = backend->check(
        source.op, source.config, iris::irsh::VoidType{},
        iris::TypeRegistry::global(), errs, {0, 0});

    if (auto* s = std::get_if<iris::irsh::StreamType>(&out)) {
        if (auto* d = iris::TypeRegistry::global().find(s->elem_id)) return d;
        return session.session_types().find(s->elem_id);
    }
    return nullptr;
}

// completion_cb and hint_cb are defined as lambdas inside run_repl (below)
// so they can capture the Session reference.  These free functions are helpers.

static Replxx::hints_t hint_cb(const std::string& input, int& ctx_len, Color& color) {
    Replxx::hints_t hints;

    // @ns.op hints — driven by BackendRegistry, not hardcoded
    auto at = input.rfind('@');
    if (at != std::string::npos) {
        std::string after(input.begin() + static_cast<int>(at) + 1, input.end());
        if (after.find('|') == std::string::npos) {
            ctx_len = static_cast<int>(after.size());
            color   = Color::BRIGHTGREEN;
            for (auto& [ns, backend] : g_registry.all()) {
                auto add = [&](std::string_view op) {
                    std::string hint = ns + "." + std::string{op};
                    if (std::string_view{hint}.starts_with(after))
                        hints.push_back(hint);
                };
                for (auto op : backend->source_ops()) add(op);
                for (auto op : backend->stage_ops())  add(op);
            }
            if (!hints.empty()) return hints;
        }
    }

    // Operator hints: after "filter <field> " offer comparison operators
    {
        std::string_view last_seg{input};
        if (auto p = input.rfind('|'); p != std::string::npos)
            last_seg = std::string_view{input}.substr(p + 1);

        static constexpr std::array ops = {
            "==", "!=", "<", ">", "<=", ">=",
            "contains", "starts_with", "ends_with", "matches"
        };

        if (last_seg.find("filter ") != std::string_view::npos &&
            !input.empty() && std::isspace((unsigned char)input.back())) {
            size_t e = input.size() - 1;
            while (e > 0 && std::isspace((unsigned char)input[e-1])) --e;
            size_t s = e;
            while (s > 0 && !std::isspace((unsigned char)input[s-1])) --s;
            std::string_view last_word{input.data() + s, e - s};
            bool is_kw = (last_word == "filter" || last_word == "&&" ||
                          last_word == "||" || last_word == "!");
            bool is_op = false;
            for (auto* op : ops) if (last_word == op) { is_op = true; break; }
            if (!is_kw && !is_op && !last_word.empty()) {
                ctx_len = 0;
                color   = Color::WHITE;
                for (auto* op : ops) hints.push_back(op);
                return hints;
            }
        }
    }

    return hints;
}

// ── Prompt helpers ────────────────────────────────────────────────────────────

// Read .git/HEAD walking up from cwd — no subprocess, no popen.
static std::string git_branch() {
    char cwd[4096];
    if (!::getcwd(cwd, sizeof(cwd))) return {};
    std::string path = cwd;
    while (path.size() > 1) {
        std::ifstream f(path + "/.git/HEAD");
        if (f.good()) {
            std::string line;
            if (!std::getline(f, line) || line.empty()) break;
            constexpr std::string_view ref_prefix = "ref: refs/heads/";
            if (line.starts_with(ref_prefix)) return line.substr(ref_prefix.size());
            return line.size() >= 7 ? line.substr(0, 7) : line; // detached HEAD short SHA
        }
        auto slash = path.rfind('/');
        if (slash == 0 || slash == std::string::npos) break;
        path.resize(slash);
    }
    return {};
}

// Current directory abbreviated: replace $HOME with ~, cap at 3 path components.
static std::string short_cwd() {
    char buf[4096];
    if (!::getcwd(buf, sizeof(buf))) return "?";
    std::string d = buf;
    if (auto* h = std::getenv("HOME"); h && d.starts_with(h))
        d = "~" + d.substr(std::strlen(h));
    size_t slashes = 0;
    for (size_t i = d.size(); i-- > 0;) {
        if (d[i] == '/' && ++slashes == 3) { d = "..." + d.substr(i); break; }
    }
    return d;
}

// Returns true when the current input is syntactically incomplete and needs more
// lines: trailing pipe/parallel/fallback operators, or unbalanced open parens.
static bool needs_continuation(std::string_view s) {
    size_t end = s.size();
    while (end > 0 && std::isspace((unsigned char)s[end - 1])) --end;
    s = s.substr(0, end);
    if (s.empty()) return false;
    int depth = 0; bool in_str = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"' && (i == 0 || s[i-1] != '\\')) in_str = !in_str;
        if (in_str) continue;
        if (c == '(') ++depth; else if (c == ')') --depth;
    }
    if (depth > 0) return true;
    char last = s.back();
    if (last == '|' || last == '&') return true;
    if (s.size() >= 2) {
        auto tail = s.substr(s.size() - 2);
        if (tail == "??" || tail == "?|") return true;
    }
    return false;
}

// Build colored prompt. Uses ANSI escape codes — replxx strips them for width calculation.
static std::string build_prompt(bool continuation) {
    if (continuation) return "\x1b[90m .. \x1b[0m";

    std::string p;
    p += "\x1b[94m" + short_cwd() + "\x1b[0m"; // bright blue dir
    auto br = git_branch();
    if (!br.empty())
        p += " \x1b[33m(" + br + ")\x1b[0m"; // yellow git branch
    p += " \x1b[32m>>\x1b[0m ";               // green >>
    return p;
}

static int run_repl(iris::irsh::Session& session) {
    iris::irsh::Checker  checker{iris::TypeRegistry::global(), session.session_types(), g_registry};
    iris::irsh::Executor exec{session, g_registry};

    Replxx rx;
    rx.install_window_change_handler();
    rx.set_highlighter_callback(highlight);

    // Completion: stage/source ops from session.imports(), @ns.op from all backends.
    rx.set_completion_callback([&session](const std::string& input, int& ctx_len) {
        Replxx::completions_t out;
        size_t word_start = input.size();
        while (word_start > 0 && !std::isspace((unsigned char)input[word_start - 1])
               && input[word_start - 1] != '|')
            --word_start;
        std::string partial{input.substr(word_start)};
        ctx_len = static_cast<int>(partial.size());

        // 1. @ns.op — anywhere user types @
        if (!partial.empty() && partial[0] == '@') {
            for (auto& [ns, backend] : g_registry.all()) {
                auto add = [&](std::string_view op) {
                    std::string cand = "@" + ns + "." + std::string{op};
                    if (std::string_view{cand}.starts_with(partial))
                        out.push_back(cand);
                };
                for (auto op : backend->source_ops()) add(op);
                for (auto op : backend->stage_ops())  add(op);
            }
            if (!out.empty()) return out;
        }

        bool in_stage_pos = (input.rfind('|') != std::string::npos);
        std::string_view last_seg{input};
        if (auto p = input.rfind('|'); p != std::string::npos)
            last_seg = std::string_view{input}.substr(p + 1);
        bool in_field_ctx =
            last_seg.find("filter ") != std::string_view::npos ||
            last_seg.find("sort ")   != std::string_view::npos ||
            last_seg.find("by: ")    != std::string_view::npos ||
            last_seg.find("select ") != std::string_view::npos;

        // 2. Field names inside filter/sort/select
        if (in_field_ctx) {
            if (auto* desc = infer_source_desc(input, session))
                for (auto& f : desc->fields)
                    if (partial.empty() || f.name.starts_with(partial))
                        out.push_back(f.name);
            return out;
        }

        // 3. File path completions: /abs, ./rel, ~/home, .. — also empty partial
        //    in argument position (e.g. "cd <Tab>") lists current directory.
        bool is_path_prefix = !partial.empty() && (partial[0] == '/' || partial[0] == '~' ||
            (partial[0] == '.' && (partial.size() == 1 || partial[1] == '/' || partial[1] == '.')));
        bool is_arg_pos = partial.empty() && !input.empty() &&
                          std::isspace((unsigned char)input.back());
        if (is_path_prefix || is_arg_pos) {
            std::string dir_part, file_part;
            auto slash = partial.rfind('/');
            if (slash == std::string::npos) {
                dir_part  = ".";
                file_part = partial;
            } else {
                dir_part  = partial.substr(0, slash == 0 ? 1 : slash);
                file_part = partial.substr(slash + 1);
            }
            if (!dir_part.empty() && dir_part[0] == '~') {
                if (const char* h = ::getenv("HOME"))
                    dir_part = std::string{h} + dir_part.substr(1);
            }
            std::error_code ec;
            for (auto& entry : std::filesystem::directory_iterator(dir_part, ec)) {
                auto name = entry.path().filename().string();
                if (!file_part.empty() && !name.starts_with(file_part)) continue;
                // No trailing '/' — user types it to descend; next Tab then re-queries contents
                std::string cand = (slash == std::string::npos)
                    ? name
                    : partial.substr(0, slash + 1) + name;
                out.push_back(cand);
            }
            return out;
        }

        // 4. Stage ops from imported backends (after |) — no early return, PATH follows
        if (in_stage_pos) {
            for (const auto& ns : session.imports())
                if (auto* b = g_registry.find(ns))
                    for (auto op : b->stage_ops())
                        if (partial.empty() || std::string_view{op}.starts_with(partial))
                            out.push_back(std::string{op});
        }

        // 5. Source ops from imported backends + keywords (source position only)
        if (!in_stage_pos) {
            for (const auto& ns : session.imports())
                if (auto* b = g_registry.find(ns))
                    for (auto op : b->source_ops())
                        if (partial.empty() || std::string_view{op}.starts_with(partial))
                            out.push_back(std::string{op});
            for (std::string_view kw : {"let", "import", "type"})
                if (partial.empty() || kw.starts_with(partial))
                    out.push_back(std::string{kw});
        }

        // 6. PATH executables — both source and stage position
        if (!partial.empty() && partial[0] != '@' && partial[0] != '$') {
            if (const char* path_env = ::getenv("PATH")) {
                std::string_view path_sv{path_env};
                std::unordered_set<std::string> seen;
                while (!path_sv.empty()) {
                    auto colon = path_sv.find(':');
                    auto dir   = std::string{path_sv.substr(0, colon)};
                    std::error_code ec;
                    for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                        if (!entry.is_regular_file(ec) && !entry.is_symlink(ec)) continue;
                        auto name = entry.path().filename().string();
                        if (name.starts_with(partial) && seen.insert(name).second)
                            out.push_back(name);
                    }
                    if (colon == std::string_view::npos) break;
                    path_sv = path_sv.substr(colon + 1);
                }
            }
        }

        // 6. $VAR completions from environment
        if (!partial.empty() && partial[0] == '$') {
            std::string_view pfx = std::string_view{partial}.substr(1);
            for (char** ep = environ; ep && *ep; ++ep) {
                std::string_view kv{*ep};
                auto eq = kv.find('=');
                if (eq == std::string_view::npos) continue;
                auto name = kv.substr(0, eq);
                if (pfx.empty() || name.starts_with(pfx))
                    out.push_back("$" + std::string{name});
            }
        }

        return out;
    });

    rx.set_unique_history(true);
    rx.set_beep_on_ambiguous_completion(false);
    rx.set_complete_on_empty(false);
    rx.set_immediate_completion(true);

    // Tab behaviour:
    //   path context (partial starts with / ./ ~/ ..)  → COMPLETE_NEXT  (cycle one-by-one)
    //   everything else                                 → COMPLETE_LINE  (fill prefix + show grid)
    // Shift+Tab always cycles backward.
    auto is_path_context = [&rx]() -> bool {
        auto st  = rx.get_state();
        auto txt = std::string_view{st.text()};
        int  cur = st.cursor_position();
        // find start of current word (back from cursor, stop at space/pipe)
        size_t w = static_cast<size_t>(cur);
        while (w > 0 && txt[w-1] != ' ' && txt[w-1] != '|') --w;
        std::string_view part = txt.substr(w, static_cast<size_t>(cur) - w);
        if (part.empty()) return true;  // arg-pos with empty partial → path list
        return part[0] == '/' || part[0] == '~' ||
               (part[0] == '.' && (part.size() == 1 || part[1] == '/' || part[1] == '.'));
    };
    rx.bind_key(Replxx::KEY::TAB, [&rx, is_path_context](char32_t) -> Replxx::ACTION_RESULT {
        if (is_path_context())
            return rx.invoke(Replxx::ACTION::COMPLETE_NEXT, 0);
        return rx.invoke(Replxx::ACTION::COMPLETE_LINE, 0);
    });
    rx.bind_key(Replxx::KEY::shift(Replxx::KEY::TAB), [&rx](char32_t) -> Replxx::ACTION_RESULT {
        return rx.invoke(Replxx::ACTION::COMPLETE_PREVIOUS, 0);
    });

    // Shared hint text: hint_cb writes here so the right-arrow handler can accept it.
    std::string pending_hint;

    // History-based full-line hint (ZSH autosuggestions pattern):
    // show the most recent history entry that starts with current input.
    rx.set_hint_callback(
        [&rx, &pending_hint](const std::string& input, int& ctx_len, Color& color) -> Replxx::hints_t {
            pending_hint.clear();
            if (!input.empty()) {
                std::string best;
                auto scan = rx.history_scan();
                while (scan.next()) {
                    std::string const& entry = scan.get().text();
                    if (entry.size() > input.size() &&
                        std::string_view{entry}.starts_with(input))
                        best = entry.substr(input.size());
                }
                if (!best.empty()) {
                    pending_hint = best;
                    ctx_len = 0;
                    color   = Color::GRAY;
                    return {std::move(best)};
                }
            }
            return hint_cb(input, ctx_len, color);
        });

    // Right arrow: if there is a pending hint, accept it whole (ZSH-style).
    // Otherwise fall through to normal cursor-right movement.
    rx.bind_key(Replxx::KEY::RIGHT,
        [&rx, &pending_hint](char32_t) -> Replxx::ACTION_RESULT {
            if (!pending_hint.empty()) {
                std::string hint = std::move(pending_hint);
                pending_hint.clear();
                for (unsigned char c : hint)
                    rx.invoke(Replxx::ACTION::INSERT_CHARACTER, char32_t(c));
                return Replxx::ACTION_RESULT::CONTINUE;
            }
            return rx.invoke(Replxx::ACTION::MOVE_CURSOR_RIGHT, 0);
        });

    rx.set_max_history_size(1000);
    rx.set_word_break_characters(" \t\n|&?=<>(){}@.");

    std::string hist_path;
    if (auto* h = std::getenv("HOME")) hist_path = std::string{h} + "/.irish_history";
    if (!hist_path.empty()) rx.history_load(hist_path);

    std::string continuation;
    while (true) {
        std::string prompt = build_prompt(continuation.empty() ? false : true);
        const char* line   = rx.input(prompt.c_str());
        if (!line) { std::putchar('\n'); break; }

        std::string_view sv{line};
        if (!sv.empty() && sv.back() == '\\') {
            continuation += sv.substr(0, sv.size() - 1);
            continuation += ' ';
            continue;
        }
        continuation += std::string{sv};
        if (needs_continuation(continuation)) {
            continuation += ' ';
            continue;
        }
        std::string input = std::move(continuation);
        continuation.clear();

        if (input.empty()) continue;
        if (input == "exit" || input == "quit") break;

        rx.history_add(input);
        repl_eval(input, session, checker, exec);
    }

    if (!hist_path.empty()) rx.history_save(hist_path);
    return 0;
}

#else  // fallback: plain fgets

static int run_repl(iris::irsh::Session& session) {
    iris::irsh::Checker  checker{iris::TypeRegistry::global(), session.session_types(), g_registry};
    iris::irsh::Executor exec{session, g_registry};

    char buf[4096];
    std::string continuation;
    while (true) {
        std::fputs(continuation.empty() ? ">> " : ".. ", stdout);
        std::fflush(stdout);
        if (!std::fgets(buf, sizeof(buf), stdin)) { std::putchar('\n'); break; }

        std::string_view sv{buf};
        if (!sv.empty() && sv.back() == '\n') sv.remove_suffix(1);
        if (!sv.empty() && sv.back() == '\\') {
            continuation += sv.substr(0, sv.size() - 1);
            continuation += ' ';
            continue;
        }
        continuation += std::string{sv};
        if (needs_continuation(continuation)) {
            continuation += ' ';
            continue;
        }
        std::string input = std::move(continuation);
        continuation.clear();

        if (input.empty()) continue;
        if (input == "exit" || input == "quit") break;
        repl_eval(input, session, checker, exec);
    }
    return 0;
}

#endif

static int run_pipeline_component(iris::irsh::Session&) {
    std::fprintf(stderr, "irish: pipeline-component mode not yet implemented\n");
    return 2;
}
