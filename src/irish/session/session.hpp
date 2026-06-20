#pragma once
#include "../checker/checker.hpp"
#include <registry.hpp>
#include <value.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace iris::irsh {

using MatVec = std::shared_ptr<std::vector<iris::IrisValue>>;

// Per-REPL-session state: lazy pipeline bindings + materialized Vecs + session type registry.
class Session {
public:
    Session();

    // Lazy pipeline bindings — bound by `let`, executed on every reference.
    void           set_pipeline(const std::string& name, TypedPipeline p);
    TypedPipeline* get_pipeline(const std::string& name);

    // Materialized Vec storage — bound by `let x = ... | collect`, replayed by reference.
    void   set_materialized(const std::string& name, std::vector<iris::IrisValue> v);
    MatVec get_materialized(const std::string& name) const;

    // Session type registry — unfrozen, accepts type declarations.
    TypeRegistry&       session_types();
    const TypeRegistry& session_types() const;

    // Auto-imported namespaces — bare words resolve against ops from these backends.
    const std::vector<std::string>& imports() const { return imports_; }
    void add_import(std::string ns) {
        if (std::find(imports_.begin(), imports_.end(), ns) == imports_.end())
            imports_.push_back(std::move(ns));
    }

    // Script arguments — set from argv after the script name.
    const std::vector<std::string>& script_args() const { return script_args_; }
    void set_script_args(std::vector<std::string> a) { script_args_ = std::move(a); }

private:
    std::unordered_map<std::string, TypedPipeline>  pipelines_;
    std::unordered_map<std::string, MatVec>          materialized_;
    TypeRegistry                                     session_reg_;
    std::vector<std::string>                         imports_{"os", "base"};
    std::vector<std::string>                         script_args_;
};

} // namespace iris::irsh
