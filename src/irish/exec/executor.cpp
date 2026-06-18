/// @file src/irish/exec/executor.cpp
#include "executor.hpp"

namespace iris::irsh {

Executor::Executor(Session& session) : session_(session) {}

// ── TODO: implement ───────────────────────────────────────────────────────────

std::expected<IrisValue, ExecError> Executor::run(const TypedPipeline&) {
    return std::unexpected(ExecError{"not implemented"});
}

std::expected<IrisValue, ExecError> Executor::run_source(const BackendRef&) {
    return std::unexpected(ExecError{"not implemented"});
}

std::expected<IrisValue, ExecError> Executor::apply_stage(const TypedStage&, IrisValue) {
    return std::unexpected(ExecError{"not implemented"});
}

} // namespace iris::irsh
