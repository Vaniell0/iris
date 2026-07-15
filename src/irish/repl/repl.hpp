/// @file src/irish/repl/repl.hpp
/// Interactive REPL — replxx-backed if IRIS_HAS_REPLXX, plain fgets fallback otherwise.
#pragma once
#include "../session/session.hpp"
#include "../backend/backend_registry.hpp"

namespace iris::irsh {

int run_repl(Session& session, BackendRegistry& registry);

} // namespace iris::irsh
