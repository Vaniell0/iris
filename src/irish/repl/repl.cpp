/// @file src/irish/repl/repl.cpp
#include "repl.hpp"
#include "eval.hpp"
#include "../lexer/lexer.hpp"
#include "../parser/parser.hpp"
#include "../parser/import_table.hpp"
#include "../checker/checker.hpp"
#include "../exec/executor.hpp"
#include <registry.hpp>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <unistd.h>

#ifdef IRIS_HAS_REPLXX
#  include <replxx.hxx>
#endif

extern char** environ;

namespace iris::irsh {

#ifdef IRIS_HAS_REPLXX

using Replxx   = replxx::Replxx;
using Color    = Replxx::Color;
using colors_t = Replxx::colors_t;

static Color token_color(TokenKind k) {
    using K = TokenKind;
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
    Lexer lexer{input};
    for (auto& t : lexer.tokenise()) {
        if (t.kind == TokenKind::Eof) break;
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
                                                      const Session& session,
                                                      BackendRegistry& registry) {
    Lexer l{input};
    auto toks = l.tokenise();

    // Parse source portion (up to first |), get BackendCall via Parser
    Parser p{toks, make_import_table(registry, session)};
    auto result = p.parse();
    if (result.program.stmts.empty()) return nullptr;

    // Extract the BackendCall for the source
    BackendCall source;
    if (auto* pipe = std::get_if<Pipeline>(&result.program.stmts[0]))
        source = pipe->source;
    else if (auto* let = std::get_if<LetStmt>(&result.program.stmts[0]))
        source = let->rhs.source;
    else
        return nullptr;

    // Ask the backend what type it produces
    auto* backend = registry.find(source.ns);
    if (!backend) return nullptr;

    std::vector<TypeError> errs;
    IrType out = backend->check(
        source.op, source.config, VoidType{},
        iris::TypeRegistry::global(), errs, {0, 0});

    if (auto* s = std::get_if<StreamType>(&out)) {
        if (auto* d = iris::TypeRegistry::global().find(s->elem_id)) return d;
        return session.session_types().find(s->elem_id);
    }
    return nullptr;
}

static Replxx::hints_t hint_cb(const std::string& input, int& ctx_len, Color& color,
                               BackendRegistry& registry) {
    Replxx::hints_t hints;

    // @ns.op hints — driven by BackendRegistry, not hardcoded
    auto at = input.rfind('@');
    if (at != std::string::npos) {
        std::string after(input.begin() + static_cast<int>(at) + 1, input.end());
        if (after.find('|') == std::string::npos) {
            ctx_len = static_cast<int>(after.size());
            color   = Color::BRIGHTGREEN;
            for (auto& [ns, backend] : registry.all()) {
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

int run_repl(Session& session, BackendRegistry& registry) {
    Checker  checker{iris::TypeRegistry::global(), session.session_types(), registry};
    Executor exec{session, registry};

    Replxx rx;
    rx.install_window_change_handler();
    rx.set_highlighter_callback(highlight);

    // Completion: stage/source ops from session.imports(), @ns.op from all backends.
    rx.set_completion_callback([&session, &registry](const std::string& input, int& ctx_len) {
        Replxx::completions_t out;
        size_t word_start = input.size();
        while (word_start > 0 && !std::isspace((unsigned char)input[word_start - 1])
               && input[word_start - 1] != '|')
            --word_start;
        std::string partial{input.substr(word_start)};
        ctx_len = static_cast<int>(partial.size());

        // 1. @ns.op — anywhere user types @
        if (!partial.empty() && partial[0] == '@') {
            for (auto& [ns, backend] : registry.all()) {
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
            if (auto* desc = infer_source_desc(input, session, registry))
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
            // "cd <Tab>" — only directories
            bool dirs_only = false;
            {
                std::string_view seg{input};
                auto p = input.rfind(';');
                if (p != std::string::npos) seg = std::string_view{input}.substr(p + 1);
                size_t s = 0;
                while (s < seg.size() && std::isspace((unsigned char)seg[s])) ++s;
                dirs_only = (seg.substr(s, 2) == "cd");
            }
            auto needs_quotes = [](const std::string& s) {
                for (unsigned char c : s)
                    if (std::isspace(c) || c == '"' || c == '\'' || c == '\\' ||
                        c == '(' || c == ')' || c == '|' || c == '&' || c == ';' || c > 127)
                        return true;
                return false;
            };
            std::error_code ec;
            for (auto& entry : std::filesystem::directory_iterator(dir_part, ec)) {
                if (dirs_only && !entry.is_directory(ec)) continue;
                auto name = entry.path().filename().string();
                if (!file_part.empty() && !name.starts_with(file_part)) continue;
                std::string cand = (slash == std::string::npos)
                    ? name
                    : partial.substr(0, slash + 1) + name;
                if (needs_quotes(cand)) cand = '"' + cand + '"';
                out.push_back(cand);
            }
            return out;
        }

        // 4. Stage ops from imported backends (after |) — no early return, PATH follows
        if (in_stage_pos) {
            for (const auto& ns : session.imports())
                if (auto* b = registry.find(ns))
                    for (auto op : b->stage_ops())
                        if (partial.empty() || std::string_view{op}.starts_with(partial))
                            out.push_back(std::string{op});
        }

        // 5. Source ops from imported backends + keywords (source position only)
        if (!in_stage_pos) {
            for (const auto& ns : session.imports())
                if (auto* b = registry.find(ns))
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
        [&rx, &pending_hint, &registry](const std::string& input, int& ctx_len, Color& color) -> Replxx::hints_t {
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
            return hint_cb(input, ctx_len, color, registry);
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
        repl_eval(input, session, checker, exec, registry);
    }

    if (!hist_path.empty()) rx.history_save(hist_path);
    return 0;
}

#else  // fallback: plain fgets

// Same shape as the replxx branch: track whether the input is syntactically
// incomplete so multi-line pipelines can span several stdin reads.
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

int run_repl(Session& session, BackendRegistry& registry) {
    Checker  checker{iris::TypeRegistry::global(), session.session_types(), registry};
    Executor exec{session, registry};

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
        repl_eval(input, session, checker, exec, registry);
    }
    return 0;
}

#endif

} // namespace iris::irsh
