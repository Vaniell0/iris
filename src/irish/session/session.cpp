/// @file src/irish/session/session.cpp
#include "session.hpp"

namespace iris::irsh {

Session::Session() = default;

void       Session::set(const std::string& name, IrisValue val) { vars_[name] = std::move(val); }
IrisValue* Session::get(const std::string& name) {
    auto it = vars_.find(name);
    return it != vars_.end() ? &it->second : nullptr;
}
bool Session::contains(const std::string& name) const { return vars_.contains(name); }

TypeRegistry&       Session::session_types()       { return session_reg_; }
const TypeRegistry& Session::session_types() const { return session_reg_; }

} // namespace iris::irsh
