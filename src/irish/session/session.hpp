#pragma once
#include <value.hpp>
#include <registry.hpp>
#include <string>
#include <unordered_map>

namespace iris::irsh {

// Per-REPL-session state: variables + live session type registry.
class Session {
public:
    Session();

    // Variable store
    void        set(const std::string& name, IrisValue val);
    IrisValue*  get(const std::string& name);
    bool        contains(const std::string& name) const;

    // Session type registry — unfrozen, accepts type declarations.
    // Global registry remains read-only.
    TypeRegistry&       session_types();
    const TypeRegistry& session_types() const;

private:
    std::unordered_map<std::string, IrisValue> vars_;
    TypeRegistry                               session_reg_;
};

} // namespace iris::irsh
