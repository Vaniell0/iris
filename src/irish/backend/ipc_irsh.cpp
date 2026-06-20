/// @file src/irish/backend/ipc_irsh.cpp
#include "ipc_irsh.hpp"
#include "../checker/checker.hpp"
#include "../checker/irtype.hpp"
#include <backend/ipc.hpp>
#include <cstdio>
#include <memory>

namespace iris::irsh {

IrType IpcIrshBackend::check(std::string_view op,
                              const BackendConfig& config,
                              const IrType& input,
                              const TypeRegistry& /*global*/,
                              std::vector<TypeError>& errs,
                              Loc loc) const {
    // op is "" (leaf form: @ipc("./sock")) or any explicit sub-op name
    (void)op;

    if (!std::get_if<std::string>(&config)) {
        errs.push_back({loc, "@ipc: expected socket path string, e.g. @ipc(\"./sock\")"});
        return VoidType{};
    }

    if (!is_stream(input) && !std::holds_alternative<AnyType>(input)) {
        errs.push_back({loc, "@ipc: input must be a typed stream"});
        return VoidType{};
    }

    // Wire-safety (Str/Bytes/CStr fields) is enforced by Checker::check_stage()
    // before this method is called, so no duplicate check needed here.
    return VoidType{};
}

IrisGen IpcIrshBackend::make_gen(std::string_view /*op*/,
                                  const BackendConfig& config,
                                  const TypeDescriptor* /*desc*/,
                                  IrisGen upstream) {
    auto* path_ptr = std::get_if<std::string>(&config);
    std::string path = path_ptr ? *path_ptr : "";

    // Drain entire upstream into the socket on first call; return nullopt always.
    // The executor's drain loop sees an empty stream; values are sent over IPC.
    return [path     = std::move(path),
            conn     = std::shared_ptr<iris::IpcBackend>{},
            upstream = std::move(upstream),
            done     = false]() mutable -> IrisResult {
        if (done) return iris_end();
        done = true;

        conn = std::make_shared<iris::IpcBackend>(iris::IpcBackend::connect(path));
        if (!conn->connected()) {
            std::fprintf(stderr, "ipc: cannot connect to '%s'\n", path.c_str());
            while (auto _r = upstream()) { if (!*_r) break; }  // drain silently
            return iris_end();
        }

        while (auto _r = upstream()) {
            if (!*_r) break;
            conn->emit(std::move(**_r));
        }
        return iris_end();
    };
}

} // namespace iris::irsh
