#pragma once
#include "../parser/ast.hpp"
#include "../checker/irtype.hpp"
#include <value.hpp>
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

// Lazy pull generator: returns nullopt at end-of-stream.
// move_only_function: generators own their position in the stream; no copies.
using IrisGen = std::move_only_function<std::optional<iris::IrisValue>()>;

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

    /// Return the op names this backend handles (used by REPL completion).
    /// Backends that don't implement this return empty — completion falls back silently.
    virtual std::vector<std::string_view> ops() const { return {}; }
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
