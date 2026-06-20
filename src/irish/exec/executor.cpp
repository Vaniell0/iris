/// @file src/irish/exec/executor.cpp
#include "executor.hpp"
#include "eval.hpp"
#include <registry.hpp>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <glob.h>
#include <unistd.h>
#include <unordered_set>
#ifdef IRIS_STDEXEC
#  include <stdexec/execution.hpp>
#  include <exec/static_thread_pool.hpp>
#  include <exec/async_scope.hpp>
#  include <exec/start_detached.hpp>
#else
#  include <thread>
#endif

namespace iris::irsh {

Executor::Executor(Session& session, BackendRegistry& registry, ExecMode mode)
    : session_(session), registry_(registry), mode_(mode) {}

// Returns the first executable found for `name` on PATH, or empty string.
static std::string find_in_path(std::string_view name) {
    if (name.empty() || name.find('/') != std::string_view::npos) return {};
    const char* path_env = ::getenv("PATH");
    if (!path_env) return {};
    std::string_view path_sv{path_env};
    while (!path_sv.empty()) {
        auto colon = path_sv.find(':');
        auto dir   = path_sv.substr(0, colon);
        std::string candidate{dir};
        candidate += '/';
        candidate += name;
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
        if (colon == std::string_view::npos) break;
        path_sv = path_sv.substr(colon + 1);
    }
    return {};
}

BackendConfig Executor::expand_exec(const BackendConfig& cfg) {
    auto* ea = std::get_if<ExecArgs>(&cfg);
    if (!ea) return cfg;
    std::vector<std::string> argv;
    const auto& sargs = session_.script_args();
    for (auto& w : *ea) {
        if (!w.is_var) {
            if (w.text.find("**") != std::string::npos) {
                auto dstar = w.text.find("/**");
                std::string base   = (dstar != std::string::npos) ? w.text.substr(0, dstar) : ".";
                std::string suffix = (dstar != std::string::npos) ? w.text.substr(dstar + 3) : "";
                if (base.empty()) base = ".";
                if (suffix.empty()) {
                    // "dir/**" with no suffix — pass the directory itself so the
                    // tool can do its own traversal (respects .gitignore etc.)
                    argv.push_back(base);
                } else {
                    // "dir/**/*.rs" — recursive expansion filtered by suffix
                    size_t before = argv.size();
                    std::error_code ec;
                    for (auto& e : std::filesystem::recursive_directory_iterator(base, ec)) {
                        if (!e.is_regular_file()) continue;
                        auto p = e.path().string();
                        if (p.ends_with(suffix)) argv.push_back(p);
                    }
                    if (argv.size() == before) argv.push_back(w.text); // keep literal if no match
                }
            } else if (w.text.find_first_of("*?[") != std::string::npos) {
                glob_t g{};
                if (::glob(w.text.c_str(), GLOB_TILDE | GLOB_NOCHECK, nullptr, &g) == 0) {
                    for (size_t i = 0; i < g.gl_pathc; ++i)
                        argv.emplace_back(g.gl_pathv[i]);
                    ::globfree(&g);
                } else {
                    ::globfree(&g);
                    argv.push_back(w.text);
                }
            } else {
                argv.push_back(w.text);
            }
            continue;
        }
        if (auto mat = session_.get_materialized(w.text)) {
            if (!mat->empty()) {
                const auto& v = (*mat)[0];
                if (v.is_str()) argv.push_back(std::get<std::string>(v.payload));
            }
        } else if (auto* p = session_.get_pipeline(w.text)) {
            auto gen_r = build_gen(*p, p->stages.size());
            if (gen_r) {
                auto gen = std::move(*gen_r);
                if (auto _r = gen(); _r && *_r)
                    if ((*_r)->is_str()) argv.push_back(std::get<std::string>((*_r)->payload));
            }
        } else if (w.text == "args") {
            for (auto& a : sargs) argv.push_back(a);
        } else if (const char* ev = ::getenv(w.text.c_str())) {
            argv.emplace_back(ev);
        }
    }
    return argv;
}

const TypeDescriptor* Executor::resolve_desc(const IrType& t) const {
    if (auto* s = std::get_if<StreamType>(&t))
        return iris::TypeRegistry::global().find(s->elem_id);
    return nullptr;
}

// ── print helpers ─────────────────────────────────────────────────────────────

static const char* type_char(int64_t t) {
    switch (t) { case 0: return "-"; case 1: return "d"; case 2: return "l"; default: return "?"; }
}

static void print_value(const iris::IrisValue& val,
                         const iris::TypeDescriptor* desc,
                         FILE* out) {
    if (!desc) {
        if (val.is_str())
            std::fprintf(out, "%s\n", std::get<std::string>(val.payload).c_str());
        return;
    }
    if (desc->name == "DirEntry") {
        auto t  = read_field("type", val, desc);
        auto sz = read_field("size", val, desc);
        auto nm = read_field("name", val, desc);
        int64_t     tv   = t  ? std::get<int64_t>(t->v)      : 0;
        int64_t     szv  = sz ? std::get<int64_t>(sz->v)     : 0;
        std::string name = nm ? std::get<std::string>(nm->v) : "?";
        std::fprintf(out, "%s  %10lld  %s\n", type_char(tv), (long long)szv, name.c_str());
        return;
    }
    if (desc->name == "ProcEntry") {
        auto pid   = read_field("pid",   val, desc);
        auto rss   = read_field("rss",   val, desc);
        auto state = read_field("state", val, desc);
        auto name  = read_field("name",  val, desc);
        int64_t     pidv   = pid   ? std::get<int64_t>(pid->v)       : 0;
        int64_t     rssv   = rss   ? std::get<int64_t>(rss->v)       : 0;
        std::string statev = state ? std::get<std::string>(state->v) : "?";
        std::string namev  = name  ? std::get<std::string>(name->v)  : "?";
        std::fprintf(out, "%6lld  %6lld kB  %s  %s\n",
            (long long)pidv, (long long)rssv, statev.c_str(), namev.c_str());
        return;
    }
    if (desc->name == "EnvEntry") {
        auto k = read_field("key", val, desc);
        auto v = read_field("val", val, desc);
        std::string key = k ? std::get<std::string>(k->v) : "?";
        std::string vv  = v ? std::get<std::string>(v->v) : "";
        std::fprintf(out, "%s=%s\n", key.c_str(), vv.c_str());
        return;
    }
    // Scalar payload (after select)
    if (val.is_str()) {
        std::fprintf(out, "%s\n", std::get<std::string>(val.payload).c_str());
        return;
    }
    std::fprintf(out, "[%s, %zu bytes]\n", desc->name.c_str(), val.raw().size());
}

// ── build_gen ─────────────────────────────────────────────────────────────────
//
// Builds a chained pull-generator for the first `stage_limit` stages of `p`.
// stage_limit == p.stages.size() processes all stages.
// Callers can pass a smaller value to stop before e.g. collect.

std::expected<IrisGen, ExecError> Executor::build_gen(const TypedPipeline& p,
                                                       size_t stage_limit) {
    const TypeDescriptor* cur_desc = resolve_desc(p.source_type);

    // Source gen
    IrisGen gen;
    if (p.source.ns == "_var") {
        if (auto mat = session_.get_materialized(p.source.op)) {
            // Materialized Vec (let x = ... | collect)
            gen = [mat, idx = size_t{0}]() mutable -> IrisResult {
                if (idx >= mat->size()) return iris_end();
                const auto& src = (*mat)[idx++];
                iris::IrisValue out;
                out.type_id = src.type_id;
                if (src.is_raw())  out.payload = src.raw();
                else if (src.is_str()) out.payload = std::get<std::string>(src.payload);
                return iris_val(std::move(out));
            };
        } else if (auto* pipe = session_.get_pipeline(p.source.op)) {
            // Lazy pipeline binding (let x = ls without collect) — re-execute inline
            auto sub_r = build_gen(*pipe, pipe->stages.size());
            if (!sub_r) return sub_r;
            gen = std::move(*sub_r);
        } else {
            // PATH grafting: unresolved name → try running as external command
            if (!find_in_path(p.source.op).empty()) {
                auto* os_b = registry_.find("os");
                if (os_b) {
                    gen = os_b->make_gen("exec", std::string{p.source.op},
                                         nullptr, nullptr);
                } else {
                    return std::unexpected(ExecError{p.source.loc,
                        "undefined variable: " + p.source.op});
                }
            } else {
                return std::unexpected(ExecError{p.source.loc,
                    "undefined variable: " + p.source.op});
            }
        }
    } else {
        auto* src_b = registry_.find(p.source.ns);
        if (!src_b)
            return std::unexpected(ExecError{p.source.loc, "@" + p.source.ns + ": unknown backend"});
        gen = src_b->make_gen(p.source.op, expand_exec(p.source.config), cur_desc, nullptr);
    }

    for (size_t i = 0; i < stage_limit && i < p.stages.size(); ++i) {
        auto& ts    = p.stages[i];
        auto& stage = ts.stage;
        if (stage.op == "print" || stage.op == "write") {
            cur_desc = resolve_desc(ts.out_type);
            continue;
        }
        auto* b = registry_.find(stage.ns);
        if (!b) return std::unexpected(ExecError{stage.loc, "@" + stage.ns + ": unknown backend"});
        gen      = b->make_gen(stage.op, expand_exec(stage.config), cur_desc, std::move(gen));
        cur_desc = resolve_desc(ts.out_type);
    }

    return gen;
}

// ── stream ────────────────────────────────────────────────────────────────────

std::expected<void, ExecError> Executor::stream(const TypedPipeline& p,
                                                  const std::string& write_path) {
    // Final descriptor: output type of last non-sink stage (sinks produce Void)
    const TypeDescriptor* final_desc = resolve_desc(p.source_type);
    for (auto& ts : p.stages)
        if (ts.stage.op != "print" && ts.stage.op != "write")
            final_desc = resolve_desc(ts.out_type);

    auto gen_result = build_gen(p, p.stages.size());
    if (!gen_result) return std::unexpected(gen_result.error());
    IrisGen gen = std::move(*gen_result);

    // In script mode, only emit output when there is an explicit sink (print or write).
    bool has_print = false;
    for (auto& ts : p.stages)
        if (ts.stage.op == "print") { has_print = true; break; }
    const bool emit = (mode_ == ExecMode::Repl) || !write_path.empty() || has_print;

    // Drain — open file lazily on first value (empty-write guarantee).
    // Propagates generator errors (unexpected) up to the REPL/script runner.
    FILE* out = nullptr;
    bool produced_any = false;
    for (;;) {
        auto _r = gen();
        if (!_r) {                  // generator error
            if (out && out != stdout) std::fclose(out);
            return std::unexpected(_r.error());
        }
        if (!*_r) break;            // end-of-stream
        produced_any = true;
        if (!emit) continue;
        if (!out) {
            if (!write_path.empty()) {
                out = std::fopen(write_path.c_str(), "w");
                if (!out)
                    return std::unexpected(ExecError{
                        {}, "write: cannot open '" + write_path + "': " + strerror(errno)});
            } else {
                out = stdout;
            }
        }
        // _var sources resolve to AnyType; recover descriptor from the first value.
        if (!final_desc)
            final_desc = iris::TypeRegistry::global().find((*_r)->type_id);
        print_value(**_r, final_desc, out);
    }
    if (out && out != stdout) std::fclose(out);

    // ?? / ?| fallbacks — fire when main pipeline produced no values
    if (!produced_any) {
        if (p.fallback_val && emit) {
            const auto& sargs = session_.script_args();
            if (auto ev = eval_expr(*p.fallback_val, &sargs)) {
                std::string s;
                std::visit([&](const auto& x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, std::string>) s = x;
                    else s = std::to_string(x);
                }, ev->v);
                std::fprintf(stdout, "%s\n", s.c_str());
            }
        } else if (p.fallback_pipe) {
            return stream(*p.fallback_pipe, write_path);
        }
    }

    return {};
}

// ── run ───────────────────────────────────────────────────────────────────────

std::expected<iris::IrisValue, ExecError> Executor::run(const TypedPipeline& p) {
    std::string write_path;
    for (auto& ts : p.stages)
        if (ts.stage.op == "write")
            if (auto* s = std::get_if<std::string>(&ts.stage.config))
                write_path = *s;

    auto result = stream(p, write_path);
    if (!result) return std::unexpected(result.error());
    return iris::IrisValue{};
}

// ── run_stmt ──────────────────────────────────────────────────────────────────

std::expected<iris::IrisValue, ExecError> Executor::run_stmt(const TypedStatement& stmt) {
    if (auto* let = std::get_if<TypedLetStmt>(&stmt)) {
        auto& rhs = let->rhs;
        bool has_collect = !rhs.stages.empty() &&
                           rhs.stages.back().stage.op == "collect";
        if (has_collect) {
            // Eagerly drain all stages before collect, store Vec in session
            auto gen_result = build_gen(rhs, rhs.stages.size() - 1);
            if (!gen_result) return std::unexpected(gen_result.error());
            std::vector<iris::IrisValue> buf;
            while (auto _r = (*gen_result)()) { if (!*_r) break; buf.push_back(std::move(**_r)); }
            session_.set_materialized(let->name, std::move(buf));
        } else {
            session_.set_pipeline(let->name, rhs);
        }
        return iris::IrisValue{};
    }
    if (auto* expr = std::get_if<TypedExprStmt>(&stmt)) {
        return run(expr->pipeline);
    }
    if (auto* imp = std::get_if<TypedImportStmt>(&stmt)) {
        // Warn about bare-word conflicts with already-imported namespaces
        if (auto* new_b = registry_.find(imp->ns)) {
            auto has_op = [](IrshBackend* b, std::string_view op) {
                for (auto o : b->source_ops()) if (o == op) return true;
                for (auto o : b->stage_ops())  if (o == op) return true;
                return false;
            };
            std::unordered_set<std::string> warned;
            auto check = [&](std::string_view new_op) {
                if (!warned.insert(std::string{new_op}).second) return;
                for (const auto& existing_ns : session_.imports()) {
                    if (existing_ns == imp->ns) continue;  // same ns — no conflict
                    if (auto* eb = registry_.find(existing_ns))
                        if (has_op(eb, new_op))
                            std::fprintf(stderr,
                                "import: bare word '%s' from '@%s' conflicts with '@%s'"
                                " — '@%s' takes precedence\n",
                                std::string{new_op}.c_str(), imp->ns.c_str(),
                                existing_ns.c_str(), existing_ns.c_str());
                }
            };
            for (auto op : new_b->source_ops()) check(op);
            for (auto op : new_b->stage_ops())  check(op);
        }
        session_.add_import(imp->ns);
        return iris::IrisValue{};
    }
    if (auto* par = std::get_if<TypedParallelStmt>(&stmt)) {
#ifdef IRIS_STDEXEC
        static exec::static_thread_pool pool;
        if (par->fire_and_forget) {
            for (auto& arm : par->arms) {
                exec::start_detached(
                    stdexec::schedule(pool.get_scheduler()) |
                    stdexec::then([this, arm]() mutable { (void)stream(arm, {}); })
                );
            }
            return iris::IrisValue{};
        }
        {
            exec::async_scope scope;
            for (auto& arm : par->arms) {
                scope.spawn(
                    stdexec::schedule(pool.get_scheduler()) |
                    stdexec::then([this, arm]() mutable { (void)stream(arm, {}); })
                );
            }
            stdexec::sync_wait(scope.on_empty());
        }
        return iris::IrisValue{};
#else
        if (par->fire_and_forget) {
            for (auto& arm : par->arms) {
                auto arm_copy = arm;
                std::thread([this, arm_copy = std::move(arm_copy)]() mutable {
                    (void)stream(arm_copy, {});
                }).detach();
            }
            return iris::IrisValue{};
        }
        for (auto& arm : par->arms) {
            auto result = run(arm);
            if (!result) return result;
        }
        return iris::IrisValue{};
#endif
    }
    return std::unexpected(ExecError{{}, "unknown statement type"});
}

} // namespace iris::irsh
