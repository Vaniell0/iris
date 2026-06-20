#pragma once
#include "../checker/checker.hpp"
#include "../session/session.hpp"
#include "../backend/backend_registry.hpp"
#include <expected>
#include <functional>
#include <string>

namespace iris::irsh {

enum class ExecMode { Repl, Script };

class Executor {
public:
    explicit Executor(Session& session, BackendRegistry& registry,
                      ExecMode mode = ExecMode::Repl);

    // Execute one type-checked pipeline, printing results to stdout.
    std::expected<iris::IrisValue, ExecError> run(const TypedPipeline& pipeline);

    // Execute one type-checked statement.
    std::expected<iris::IrisValue, ExecError> run_stmt(const TypedStatement& stmt);

private:
    Session&         session_;
    BackendRegistry& registry_;
    ExecMode         mode_;

    std::expected<void, ExecError>      stream(const TypedPipeline&, const std::string& write_path);
    std::expected<IrisGen, ExecError>   build_gen(const TypedPipeline&, size_t stage_limit);

    const TypeDescriptor* resolve_desc(const IrType& t) const;
};

} // namespace iris::irsh
