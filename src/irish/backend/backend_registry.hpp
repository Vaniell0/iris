#pragma once
#include "../parser/ast.hpp"
#include "../checker/irtype.hpp"
#include <value.hpp>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace iris { struct TypeDescriptor; class TypeRegistry; }

namespace iris::irsh {

struct TypeError;

struct ExecError { Loc loc; std::string msg; int exit_code = 1; };

// IrisResult: either a value (or end-of-stream via empty optional) or an error.
// Enables the ?? / ?| error-fallback operators without P2300.
using IrisResult = std::expected<std::optional<iris::IrisValue>, ExecError>;

// Lazy pull generator: returns empty optional at end-of-stream, unexpected on error.
// move_only_function: generators own their position in the stream; no copies.
using IrisGen = std::move_only_function<IrisResult()>;

// ── Generator helpers ─────────────────────────────────────────────────────────

// Signal end-of-stream.
inline IrisResult iris_end() noexcept { return std::optional<iris::IrisValue>{}; }
// Emit one value downstream.
inline IrisResult iris_val(iris::IrisValue v) { return std::optional<iris::IrisValue>{std::move(v)}; }
// Propagate a runtime error through the pipeline.
inline IrisResult iris_err(ExecError e) { return std::unexpected(std::move(e)); }

// ── IrshBackend ───────────────────────────────────────────────────────────────
//
// Every @ns.* namespace is handled by one IrshBackend.
// check()    — parse time type checking.
// make_gen() — execute time: builds a lazy pull generator.
//   upstream = nullptr for sources (ls, ps, env).
//   upstream ≠ nullptr for transforms (filter, sort, …).

class IrshBackend {
public:
    virtual ~IrshBackend() = default;
    virtual std::string_view name() const = 0;

    virtual IrType check(std::string_view op,
                         const BackendConfig& config,
                         const IrType& input,
                         const TypeRegistry& global,
                         std::vector<TypeError>& errs,
                         Loc loc) const = 0;

    virtual IrisGen make_gen(std::string_view op,
                              const BackendConfig& config,
                              const TypeDescriptor* desc,
                              IrisGen upstream) = 0;

    // Config syntax kind — tells the parser what to parse after the op name.
    // The parser dispatches on this so backends don't need special-casing.
    enum class ConfigKind {
        None,       // no args: collect, print, types, ps, env, clear
        String,     // bare string/path: write "f.txt", ls "/tmp"
        LsArgs,     // optional flags (-la) + optional path: ls
        Expr,       // expression tree: filter size > 0
        FieldList,  // {f1, f2, ...}: map
        SortArg,    // [by:] field [desc]: sort
        ExecArgs,   // (word $var ...): exec
        TypeName,   // (TypeId) or bare ident: type(T), parse(T)
        Lit,        // (string | int | $var): lit
        IntExpr,    // integer or $var or (integer): head
    };

    struct OpDesc {
        std::string_view name;
        bool       as_source;
        bool       as_stage;
        ConfigKind config = ConfigKind::None;
    };

    /// Single source of truth for all ops. source_ops/stage_ops derive from this.
    virtual std::vector<OpDesc> ops() const { return {}; }

    std::vector<std::string_view> source_ops() const {
        std::vector<std::string_view> r;
        for (auto& d : ops()) if (d.as_source) r.push_back(d.name);
        return r;
    }
    std::vector<std::string_view> stage_ops() const {
        std::vector<std::string_view> r;
        for (auto& d : ops()) if (d.as_stage) r.push_back(d.name);
        return r;
    }
};

// ── BackendRegistry ───────────────────────────────────────────────────────────

class BackendRegistry {
public:
    void register_backend(std::unique_ptr<IrshBackend> b) {
        if (frozen_) throw std::logic_error("BackendRegistry: sealed — cannot register after freeze()");
        auto name = std::string{b->name()};
        backends_[name] = std::move(b);
    }

    IrshBackend* find(std::string_view ns) const {
        auto it = backends_.find(std::string{ns});
        return it != backends_.end() ? it->second.get() : nullptr;
    }

    const std::unordered_map<std::string, std::unique_ptr<IrshBackend>>&
    all() const { return backends_; }

    /// Seal the registry. After freeze(), register_backend() throws.
    /// Called once in main after all built-in and plugin backends are registered.
    void freeze() noexcept { frozen_ = true; }
    bool is_frozen() const noexcept { return frozen_; }

private:
    std::unordered_map<std::string, std::unique_ptr<IrshBackend>> backends_;
    bool frozen_ = false;
};

} // namespace iris::irsh
