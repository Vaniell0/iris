/// @file src/irish/main.cpp
/// Entry point — three-mode detection, then dispatch.
#include "session/session.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "checker/checker.hpp"
#include "exec/executor.hpp"
#include <backend/os.hpp>
#include <registry.hpp>
#include <unistd.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#ifdef IRIS_HAS_REPLXX
#  include <replxx.hxx>
#endif

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

// ── Demo interpreter — direct AST eval, bypasses checker/executor ─────────────
// Removed once Executor is implemented.

namespace demo {

using namespace iris::irsh;

// Scalar value for expression evaluation
struct Val {
    std::variant<int64_t, double, bool, std::string> v;

    bool as_bool() const {
        return std::visit([](auto& x) -> bool {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, bool>)        return x;
            if constexpr (std::is_same_v<T, int64_t>)     return x != 0;
            if constexpr (std::is_same_v<T, double>)      return x != 0.0;
            if constexpr (std::is_same_v<T, std::string>) return !x.empty();
        }, v);
    }
};

// Resolve a field name against a DirEntry
static std::optional<Val> field_dir(std::string_view name, const DirEntry& e) {
    if (name == "name")  return Val{std::string{e.name}};
    if (name == "size")  return Val{e.size};
    if (name == "mtime") return Val{e.mtime};
    if (name == "type")  return Val{(int64_t)e.type};
    if (name == "mode")  return Val{(int64_t)e.mode};
    return std::nullopt;
}

static std::optional<Val> field_proc(std::string_view name, const ProcEntry& e) {
    if (name == "pid")   return Val{(int64_t)e.pid};
    if (name == "ppid")  return Val{(int64_t)e.ppid};
    if (name == "rss")   return Val{e.rss};
    if (name == "name")  return Val{std::string{e.name}};
    if (name == "state") return Val{std::string{e.state}};
    return std::nullopt;
}

template<typename F>
static std::optional<Val> eval_expr(const Expr& expr, F field_lookup) {
    return std::visit([&](auto& node) -> std::optional<Val> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IntLit>)  return Val{node.value};
        if constexpr (std::is_same_v<T, FloatLit>) return Val{node.value};
        if constexpr (std::is_same_v<T, StrLit>)  return Val{node.value};
        if constexpr (std::is_same_v<T, BoolLit>) return Val{node.value};
        if constexpr (std::is_same_v<T, FieldRef>) return field_lookup(node.name);
        if constexpr (std::is_same_v<T, std::unique_ptr<UnOp>>) {
            auto v = eval_expr(node->operand, field_lookup);
            if (!v) return std::nullopt;
            return Val{!v->as_bool()};
        }
        if constexpr (std::is_same_v<T, std::unique_ptr<BinOp>>) {
            if (node->op == BinOpKind::And) {
                auto l = eval_expr(node->lhs, field_lookup);
                if (!l || !l->as_bool()) return Val{false};
                auto r = eval_expr(node->rhs, field_lookup);
                return r ? Val{r->as_bool()} : Val{false};
            }
            if (node->op == BinOpKind::Or) {
                auto l = eval_expr(node->lhs, field_lookup);
                if (l && l->as_bool()) return Val{true};
                auto r = eval_expr(node->rhs, field_lookup);
                return r ? Val{r->as_bool()} : Val{false};
            }
            auto l = eval_expr(node->lhs, field_lookup);
            auto r = eval_expr(node->rhs, field_lookup);
            if (!l || !r) return std::nullopt;
            // compare same-type values
            return std::visit([&](auto& lv) -> std::optional<Val> {
                using LT = std::decay_t<decltype(lv)>;
                if (auto* rv = std::get_if<LT>(&r->v)) {
                    switch (node->op) {
                        case BinOpKind::Eq: return Val{lv == *rv};
                        case BinOpKind::Ne: return Val{lv != *rv};
                        case BinOpKind::Lt: return Val{lv <  *rv};
                        case BinOpKind::Le: return Val{lv <= *rv};
                        case BinOpKind::Gt: return Val{lv >  *rv};
                        case BinOpKind::Ge: return Val{lv >= *rv};
                        default: return std::nullopt;
                    }
                }
                // int64 vs double coercion
                if constexpr (std::is_same_v<LT, int64_t>) {
                    if (auto* rv = std::get_if<double>(&r->v)) {
                        double lf = static_cast<double>(lv);
                        switch (node->op) {
                            case BinOpKind::Eq: return Val{lf == *rv};
                            case BinOpKind::Ne: return Val{lf != *rv};
                            case BinOpKind::Lt: return Val{lf <  *rv};
                            case BinOpKind::Le: return Val{lf <= *rv};
                            case BinOpKind::Gt: return Val{lf >  *rv};
                            case BinOpKind::Ge: return Val{lf >= *rv};
                            default: return std::nullopt;
                        }
                    }
                }
                return std::nullopt;
            }, l->v);
        }
        return std::nullopt;
    }, expr);
}

static const char* type_char(int32_t t) {
    switch (t) { case 0: return "-"; case 1: return "d"; case 2: return "l"; default: return "?"; }
}

static void print_dir_entry(const DirEntry& e) {
    std::printf("%s  %10lld  %s\n", type_char(e.type), (long long)e.size, e.name);
}

static void print_proc_entry(const ProcEntry& e) {
    std::printf("%6d  %6lld kB  %s  %s\n", e.pid, (long long)e.rss, e.state, e.name);
}

static std::string strip_quotes(std::string_view s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return std::string{s.substr(1, s.size() - 2)};
    return std::string{s};
}

static void eval_pipeline(const Pipeline& p) {
    // Extract stages
    int64_t head_limit = INT64_MAX;
    const Expr* filter_pred = nullptr;
    for (auto& stage : p.stages) {
        if (auto* h = std::get_if<HeadStage>(&stage))   head_limit = h->n;
        if (auto* f = std::get_if<FilterStage>(&stage)) filter_pred = &f->predicate;
    }

    if (p.source.ns == "os" && p.source.op == "ls") {
        std::string path = strip_quotes(p.source.config.empty() ? "." : p.source.config);
        iris::os::LsStream stream{path};
        if (!stream.open()) { std::fprintf(stderr, "ls: cannot open '%s'\n", path.c_str()); return; }
        int64_t count = 0;
        while (count < head_limit) {
            auto entry = stream.next();
            if (!entry) break;
            if (filter_pred) {
                auto v = eval_expr(*filter_pred, [&](std::string_view n) { return field_dir(n, *entry); });
                if (!v || !v->as_bool()) continue;
            }
            print_dir_entry(*entry);
            ++count;
        }
        return;
    }

    if (p.source.ns == "os" && p.source.op == "ps") {
        iris::os::PsStream stream{};
        if (!stream.open()) { std::fprintf(stderr, "ps: cannot open /proc\n"); return; }
        int64_t count = 0;
        while (count < head_limit) {
            auto entry = stream.next();
            if (!entry) break;
            if (filter_pred) {
                auto v = eval_expr(*filter_pred, [&](std::string_view n) { return field_proc(n, *entry); });
                if (!v || !v->as_bool()) continue;
            }
            print_proc_entry(*entry);
            ++count;
        }
        return;
    }

    if (p.source.ns == "os" && p.source.op == "env") {
        iris::os::EnvStream stream{};
        stream.open();
        int64_t count = 0;
        while (count < head_limit) {
            auto entry = stream.next();
            if (!entry) break;
            if (filter_pred) {
                auto field = [&](std::string_view n) -> std::optional<Val> {
                    if (n == "key") return Val{std::string{entry->key}};
                    if (n == "val") return Val{std::string{entry->val}};
                    return std::nullopt;
                };
                auto v = eval_expr(*filter_pred, field);
                if (!v || !v->as_bool()) continue;
            }
            std::printf("%s=%s\n", entry->key, entry->val);
            ++count;
        }
        return;
    }

    std::fprintf(stderr, "eval: @%s.%s not supported in demo mode\n",
        p.source.ns.c_str(), p.source.op.c_str());
}

// ── Session variable store ────────────────────────────────────────────────────
// Lazy: stores pipeline AST only; nothing executes at bind time.
// Re-executed every time the variable is referenced.

static std::unordered_map<std::string, Pipeline> pipeline_vars;

static void eval_statement(const Statement& stmt) {
    if (auto* p   = std::get_if<Pipeline>(&stmt)) { eval_pipeline(*p); return; }
    if (auto* let = std::get_if<LetStmt>(&stmt))  {
        pipeline_vars[let->name] = let->rhs;   // bind only, do not execute
        return;
    }
    std::fprintf(stderr, "eval: statement type not yet supported\n");
}

} // namespace demo

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

// ── REPL helpers ─────────────────────────────────────────────────────────────

static void repl_eval(const std::string& input) {
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

    // bare variable reference: "x" or "x | head 3"
    // check if input starts with a known session variable name
    {
        auto sp = input.find_first_of(" \t|");
        std::string name = (sp == std::string::npos) ? input : input.substr(0, sp);
        auto it = demo::pipeline_vars.find(name);
        if (it != demo::pipeline_vars.end()) {
            // if there are extra stages after the name, append them
            if (sp != std::string::npos) {
                std::string suffix = "@_placeholder " + input.substr(sp);
                // re-parse with the variable's source substituted
                iris::irsh::Pipeline composed = it->second;
                iris::irsh::Lexer lx{input.substr(sp + 1)};
                iris::irsh::Parser px{lx.tokenise()};
                // parse just the extra stages ("|stage1|stage2...")
                // simpler: parse a full pipeline from "source | extra_stages" using placeholder
                // For now parse the suffix as extra stages only
                std::string fake = "@_var | " + input.substr(sp + 1);
                iris::irsh::Lexer flx{fake};
                iris::irsh::Parser fpx{flx.tokenise()};
                auto fr = fpx.parse();
                if (fr.ok() && !fr.program.stmts.empty()) {
                    if (auto* fp = std::get_if<iris::irsh::Pipeline>(&fr.program.stmts[0])) {
                        composed.stages.insert(composed.stages.end(),
                            fp->stages.begin(), fp->stages.end());
                    }
                }
                demo::eval_pipeline(composed);
            } else {
                demo::eval_pipeline(it->second);
            }
            return;
        }
    }

    iris::irsh::Lexer lexer{input};
    auto tokens = lexer.tokenise();
    iris::irsh::Parser parser{std::move(tokens)};
    auto result = parser.parse();
    if (!result.ok()) {
        for (auto& e : result.errors)
            std::fprintf(stderr, "  error %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
    } else {
        for (auto& stmt : result.program.stmts)
            demo::eval_statement(stmt);
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
        // t.col is 1-based; text position in the full input string
        auto pos = static_cast<size_t>(t.text.data() - input.data());
        Color c = token_color(t.kind);
        for (size_t i = 0; i < t.text.size() && pos + i < colors.size(); ++i)
            colors[pos + i] = c;
    }
}

static Replxx::hints_t hint_cb(const std::string& input, int& ctx_len, Color& color) {
    Replxx::hints_t hints;
    auto at = input.rfind('@');
    if (at != std::string::npos) {
        // after = everything the user typed after the last '@'
        std::string after(input.begin() + static_cast<int>(at) + 1, input.end());
        // ctx_len = chars to replace from end of input (just `after`, not the '@')
        ctx_len = static_cast<int>(after.size());
        color   = Color::BRIGHTGREEN;
        for (auto* h : {"os.ls", "os.ps", "os.env", "os.exec"})
            if (std::string_view{h}.starts_with(after)) hints.push_back(h);
    }
    return hints;
}

static int run_repl(iris::irsh::Session&) {
    Replxx rx;
    rx.install_window_change_handler();
    rx.set_highlighter_callback(highlight);
    rx.set_hint_callback(hint_cb);
    rx.set_max_history_size(1000);
    rx.set_word_break_characters(" \t\n|&?=<>(){}@.");

    // load history
    std::string hist_path;
    if (auto* h = std::getenv("HOME")) hist_path = std::string{h} + "/.irish_history";
    if (!hist_path.empty()) rx.history_load(hist_path);

    std::string continuation;
    while (true) {
        const char* prompt = continuation.empty() ? ">> " : ".. ";
        const char* line   = rx.input(prompt);
        if (!line) { std::putchar('\n'); break; }   // Ctrl+D

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
        repl_eval(input);
    }

    if (!hist_path.empty()) rx.history_save(hist_path);
    return 0;
}

#else  // fallback: plain fgets

static int run_repl(iris::irsh::Session&) {
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
        repl_eval(input);
    }
    return 0;
}

#endif

static int run_pipeline_component(iris::irsh::Session&) {
    std::fprintf(stderr, "irish: pipeline-component mode not yet implemented\n");
    return 2;
}
