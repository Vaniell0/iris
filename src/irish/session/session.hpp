#pragma once
#include "../checker/checker.hpp"
#include <registry.hpp>
#include <value.hpp>
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

private:
    std::unordered_map<std::string, TypedPipeline>  pipelines_;
    std::unordered_map<std::string, MatVec>          materialized_;
    TypeRegistry                                     session_reg_;
};

} // namespace iris::irsh
