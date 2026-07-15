/// @file src/irish/repl/eval.hpp
/// Shared line-eval used by both script and REPL modes.
#pragma once
#include "../session/session.hpp"
#include "../checker/checker.hpp"
#include "../exec/executor.hpp"
#include "../backend/backend_registry.hpp"
#include <string>

namespace iris::irsh {

// Evaluate a single line of irsh source.
//
// Handles built-in directives (`cd`, `:types`, `:type`, `:lex`), variable
// references from Session, and the normal parse → check → execute path.
//
// Returns exit code: 0 success, 1 runtime error, 2 parse/type error.
int repl_eval(const std::string& input,
              Session& session,
              Checker& checker,
              Executor& exec,
              BackendRegistry& registry);

} // namespace iris::irsh
