/// @file src/irish/exec/executor.cpp
#include "executor.hpp"
#include "eval.hpp"
#include <registry.hpp>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace iris::irsh {

Executor::Executor(Session& session, BackendRegistry& registry, ExecMode mode)
    : session_(session), registry_(registry), mode_(mode) {}

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
        auto mat = session_.get_materialized(p.source.op);
        if (!mat)
            return std::unexpected(ExecError{"undefined variable: " + p.source.op});
        gen = [mat, idx = size_t{0}]() mutable -> std::optional<iris::IrisValue> {
            if (idx >= mat->size()) return std::nullopt;
            const auto& src = (*mat)[idx++];
            iris::IrisValue out;
            out.type_id = src.type_id;
            if (src.is_raw())
                out.payload = src.raw();  // IrisBuffer: shared_ptr refcount++
            else if (src.is_str())
                out.payload = std::get<std::string>(src.payload);
            return out;
        };
    } else {
        auto* src_b = registry_.find(p.source.ns);
        if (!src_b)
            return std::unexpected(ExecError{"@" + p.source.ns + ": unknown backend"});
        gen = src_b->make_gen(p.source.op, p.source.config, cur_desc, nullptr);
    }

    for (size_t i = 0; i < stage_limit && i < p.stages.size(); ++i) {
        auto& ts    = p.stages[i];
        auto& stage = ts.stage;
        if (stage.op == "print" || stage.op == "write") {
            cur_desc = resolve_desc(ts.out_type);
            continue;
        }
        auto* b = registry_.find(stage.ns);
        if (!b) return std::unexpected(ExecError{"@" + stage.ns + ": unknown backend"});
        gen      = b->make_gen(stage.op, stage.config, cur_desc, std::move(gen));
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

    // Drain — open file lazily on first value (empty-write guarantee)
    FILE* out = nullptr;
    while (auto v = gen()) {
        if (!emit) continue;
        if (!out) {
            if (!write_path.empty()) {
                out = std::fopen(write_path.c_str(), "w");
                if (!out)
                    return std::unexpected(ExecError{
                        "write: cannot open '" + write_path + "': " + strerror(errno)});
            } else {
                out = stdout;
            }
        }
        // _var sources resolve to AnyType; recover descriptor from the first value.
        if (!final_desc)
            final_desc = iris::TypeRegistry::global().find(v->type_id);
        print_value(*v, final_desc, out);
    }
    if (out && out != stdout) std::fclose(out);

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
            while (auto v = (*gen_result)()) buf.push_back(std::move(*v));
            session_.set_materialized(let->name, std::move(buf));
        } else {
            session_.set_pipeline(let->name, rhs);
        }
        return iris::IrisValue{};
    }
    if (auto* expr = std::get_if<TypedExprStmt>(&stmt)) {
        return run(expr->pipeline);
    }
    return std::unexpected(ExecError{"unknown statement type"});
}

} // namespace iris::irsh
