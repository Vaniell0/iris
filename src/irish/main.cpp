/// @file src/irish/main.cpp
/// Entry point — three-mode detection, then dispatch.
#include "session/session.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "checker/checker.hpp"
#include "exec/executor.hpp"
#include "backend/os_irsh.hpp"
#include "backend/base_irsh.hpp"
#include "backend/plugin_loader.hpp"
#include <registry.hpp>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#ifdef IRIS_HAS_REPLXX
#  include <replxx.hxx>
#endif

static iris::irsh::BackendRegistry g_registry;

static int run_script(const char* path, iris::irsh::Session& session);
static int run_repl(iris::irsh::Session& session);
static int run_pipeline_component(iris::irsh::Session& session);
static void repl_eval(const std::string& input, iris::irsh::Session& session,
                      iris::irsh::Checker& checker, iris::irsh::Executor& exec);

int main(int argc, char** argv) {
    // Session must be created before BaseIrshBackend so its registry reference is stable
    iris::irsh::Session session;

    // Register built-in backends (BaseIrshBackend holds TypeRegistry refs — must outlive registry)
    g_registry.register_backend(std::make_unique<iris::irsh::BaseIrshBackend>(
        iris::TypeRegistry::global(), session.session_types()));
    g_registry.register_backend(std::make_unique<iris::irsh::OsIrshBackend>());

    // Load plugins from ~/.iris/plugins/*.so (non-fatal: warn and continue)
    for (auto& err : iris::irsh::load_plugins(g_registry))
        std::fprintf(stderr, "irish: plugin warning: %s\n", err.c_str());

    g_registry.freeze();
    iris::TypeRegistry::global().freeze();

    if (argc > 1 && std::string_view{argv[1]} == "-e" && argc > 2) {
        iris::irsh::Checker  checker{iris::TypeRegistry::global(), session.session_types(), g_registry};
        iris::irsh::Executor exec{session, g_registry};
        repl_eval(argv[2], session, checker, exec);
        return 0;
    }
    if (argc > 1) return run_script(argv[1], session);
    if (isatty(STDIN_FILENO)) return run_repl(session);
    return run_pipeline_component(session);
}

static int run_script(const char* path, iris::irsh::Session& session) {
    std::ifstream f{path};
    if (!f) { std::fprintf(stderr, "irish: cannot open '%s'\n", path); return 1; }
    iris::irsh::Checker  checker{iris::TypeRegistry::global(), session.session_types(), g_registry};
    iris::irsh::Executor exec{session, g_registry};
    std::string continuation, line;
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
        repl_eval(input, session, checker, exec);
    }
    if (!continuation.empty()) repl_eval(continuation, session, checker, exec);
    return 0;
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

// ── REPL eval ─────────────────────────────────────────────────────────────────

static void repl_eval(const std::string& input,
                      iris::irsh::Session& session,
                      iris::irsh::Checker& checker,
                      iris::irsh::Executor& exec) {
    if (input == ":types") {
        auto& reg = iris::TypeRegistry::global();
        for (auto& [id, d] : reg.all()) {
            std::printf("struct %s {\n", d.name.c_str());
            for (auto& f : d.fields)
                std::printf("    %-12s : %-6s  offset=%-4zu  size=%zu\n",
                    f.name.c_str(), kind_name(f.kind), f.offset, f.size);
            std::printf("}\n");
        }
        return;
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
        return;
    }
    if (input.starts_with(":lex ")) {
        iris::irsh::Lexer lexer{std::string_view{input}.substr(5)};
        for (auto& t : lexer.tokenise()) {
            if (t.kind == iris::irsh::TokenKind::Eof) break;
            std::printf("  %-14s  %.*s\n",
                token_kind_name(t.kind),
                static_cast<int>(t.text.size()), t.text.data());
        }
        return;
    }

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
        iris::irsh::Parser fpx{flx.tokenise()};
        auto fr = fpx.parse();
        if (fr.ok() && !fr.program.stmts.empty()) {
            auto typed = checker.check(fr.program);
            if (typed.ok())
                for (auto& stmt : typed.stmts) {
                    auto res = exec.run_stmt(stmt);
                    if (!res) std::fprintf(stderr, "error: %s\n", res.error().msg.c_str());
                }
            else for (auto& e : typed.errors)
                std::fprintf(stderr, "  type error %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
        }
        return;
    }
    if (auto* tp = session.get_pipeline(name)) {
            iris::irsh::TypedPipeline composed = *tp;
            if (pipe_pos != std::string::npos) {
                // Parse extra stages appended after '|' using a dummy source.
                std::string fake = "@_var | " + input.substr(pipe_pos + 1);
                iris::irsh::Lexer flx{fake};
                iris::irsh::Parser fpx{flx.tokenise()};
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
            if (!res) std::fprintf(stderr, "error: %s\n", res.error().msg.c_str());
            return;
        }
    }

    // Normal parse → check → execute
    iris::irsh::Lexer lexer{input};
    auto tokens = lexer.tokenise();
    iris::irsh::Parser parser{std::move(tokens)};
    auto parse_result = parser.parse();

    if (!parse_result.ok()) {
        for (auto& e : parse_result.errors)
            std::fprintf(stderr, "  error %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
        return;
    }

    auto typed = checker.check(parse_result.program);
    if (!typed.ok()) {
        for (auto& e : typed.errors)
            std::fprintf(stderr, "  type error %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
        return;
    }

    for (auto& stmt : typed.stmts) {
        auto res = exec.run_stmt(stmt);
        if (!res) std::fprintf(stderr, "error: %s\n", res.error().msg.c_str());
    }
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

static void highlight(const std::string& input, colors_t& colors) {
    iris::irsh::Lexer lexer{input};
    for (auto& t : lexer.tokenise()) {
        if (t.kind == iris::irsh::TokenKind::Eof) break;
        auto pos = static_cast<size_t>(t.text.data() - input.data());
        Color c = token_color(t.kind);
        for (size_t i = 0; i < t.text.size() && pos + i < colors.size(); ++i)
            colors[pos + i] = c;
    }
}

// Walk tokens before the first | to identify the source backend's element type.
// Used for tab completion and operator hints.
static const iris::TypeDescriptor* infer_source_desc(const std::string& input) {
    iris::irsh::Lexer l{input};
    auto toks = l.tokenise();

    // Parse source portion (up to first |), get BackendCall via Parser
    iris::irsh::Parser p{toks};
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

    if (auto* s = std::get_if<iris::irsh::StreamType>(&out))
        return iris::TypeRegistry::global().find(s->elem_id);
    return nullptr;
}

static Replxx::completions_t completion_cb(const std::string& input, int& ctx_len) {
    Replxx::completions_t out;

    // Partial word being typed (chars after last whitespace / pipeline chars)
    size_t word_start = input.size();
    while (word_start > 0 && !std::isspace((unsigned char)input[word_start - 1])
           && input[word_start - 1] != '|')
        --word_start;
    std::string partial{input.substr(word_start)};
    ctx_len = static_cast<int>(partial.size());

    // 1. @ns.op — anywhere user types @; skip os (accessed via bare shorthands)
    if (!partial.empty() && partial[0] == '@') {
        for (auto& [ns, backend] : g_registry.all()) {
            if (ns == "os") continue;
            for (auto op : backend->ops()) {
                std::string cand = "@" + ns + "." + std::string{op};
                if (std::string_view{cand}.starts_with(partial))
                    out.push_back(cand);
            }
        }
        if (!out.empty()) return out;
    }

    // Determine pipeline context
    bool in_stage_pos = (input.rfind('|') != std::string::npos);
    std::string_view last_seg{input};
    if (auto p = input.rfind('|'); p != std::string::npos)
        last_seg = std::string_view{input}.substr(p + 1);

    bool in_field_ctx =
        last_seg.find("filter ")  != std::string_view::npos ||
        last_seg.find("sort ")    != std::string_view::npos ||
        last_seg.find("by: ")     != std::string_view::npos ||
        last_seg.find("select ")  != std::string_view::npos;

    // 2. Field name completion inside filter/sort/select
    if (in_field_ctx) {
        const iris::TypeDescriptor* desc = infer_source_desc(input);
        if (desc)
            for (auto& f : desc->fields)
                if (partial.empty() || f.name.starts_with(partial))
                    out.push_back(f.name);
        return out;
    }

    // 3. Stage names after | — derive from canonical shorthand table (as_stage only)
    if (in_stage_pos) {
        for (auto& sh : iris::irsh::k_shorthands)
            if (sh.as_stage && (partial.empty() || sh.name.starts_with(partial)))
                out.push_back(std::string{sh.name});
        if (!out.empty()) return out;
    }

    // 4. Source position: source shorthands from canonical table + let keyword
    for (auto& sh : iris::irsh::k_shorthands)
        if (sh.as_source && (partial.empty() || sh.name.starts_with(partial)))
            out.push_back(std::string{sh.name});
    if (partial.empty() || std::string_view{"let"}.starts_with(partial))
        out.push_back("let");
    return out;
}

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
                if (ns == "os") continue; // os accessed via bare shorthands
                for (auto op : backend->ops()) {
                    std::string hint = ns + "." + std::string{op};
                    if (std::string_view{hint}.starts_with(after))
                        hints.push_back(hint);
                }
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
    rx.set_completion_callback(completion_cb);
    rx.set_unique_history(true);

    // History-based full-line hint (ZSH autosuggestions pattern):
    // show the most recent history entry that starts with current input.
    rx.set_hint_callback(
        [&rx](const std::string& input, int& ctx_len, Color& color) -> Replxx::hints_t {
            if (!input.empty()) {
                std::string best;
                auto scan = rx.history_scan();
                while (scan.next()) {
                    std::string const& entry = scan.get().text();
                    if (entry.size() > input.size() &&
                        std::string_view{entry}.starts_with(input))
                        best = entry.substr(input.size()); // keep last (most recent) match
                }
                if (!best.empty()) {
                    ctx_len = 0;
                    color   = Color::GRAY;
                    return {std::move(best)};
                }
            }
            // Fall through to @ns.op and operator hints
            return hint_cb(input, ctx_len, color);
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
        std::string input = continuation + std::string{sv};
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
        std::string input = continuation + std::string{sv};
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
