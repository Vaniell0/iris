/// @file src/irish/session/session.cpp
#include "session.hpp"

namespace iris::irsh {

Session::Session() = default;

void Session::set_pipeline(const std::string& name, TypedPipeline p) {
    pipelines_[name] = std::move(p);
}

TypedPipeline* Session::get_pipeline(const std::string& name) {
    auto it = pipelines_.find(name);
    return it != pipelines_.end() ? &it->second : nullptr;
}

void Session::set_materialized(const std::string& name, std::vector<iris::IrisValue> v) {
    materialized_[name] = std::make_shared<std::vector<iris::IrisValue>>(std::move(v));
}

MatVec Session::get_materialized(const std::string& name) const {
    auto it = materialized_.find(name);
    return it != materialized_.end() ? it->second : nullptr;
}

TypeRegistry&       Session::session_types()       { return session_reg_; }
const TypeRegistry& Session::session_types() const { return session_reg_; }

} // namespace iris::irsh
