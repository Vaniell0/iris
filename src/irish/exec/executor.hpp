#pragma once
#include "../checker/checker.hpp"
#include "../session/session.hpp"
#include <expected>
#include <string>

namespace iris::irsh {

struct ExecError {
    std::string msg;
    int         exit_code = 1; // 1=runtime 2=parse/type 3=backend unavailable
};

class Executor {
public:
    explicit Executor(Session& session);

    // Execute one already-type-checked pipeline.
    // Returns the final IrisValue (Void if the pipeline ends with a sink).
    std::expected<IrisValue, ExecError> run(const TypedPipeline& pipeline);

private:
    Session& session_;

    std::expected<IrisValue, ExecError> run_source(const BackendRef&);
    std::expected<IrisValue, ExecError> apply_stage(const TypedStage&, IrisValue);
};

} // namespace iris::irsh
