/// @file   include/execution.hpp
/// @brief  P2300 (std::execution) sender adaptors for iris Backends.
///
/// Enabled when IRIS_STDEXEC=1 (cmake -DIRIS_STDEXEC=ON).
///
/// Usage:
///   auto result = iris::sync_wait(
///       iris::just(val) | iris::via(pipeline) | iris::then(transform)
///   );
///
/// iris::via(b) calls b.emit(val) then b.recv() — a synchronous round-trip.
/// Works with PipedFn, IpcBackend, and any Backend with symmetric emit/recv.
/// For standalone FnBackend (asymmetric channels) use iris::then directly.

#pragma once
#ifndef IRIS_STDEXEC
#  error "iris/execution.hpp requires stdexec — rebuild with -DIRIS_STDEXEC=ON (see flake.nix)"
#endif

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <backend.hpp>
#include <value.hpp>
#include <optional>
#include <tuple>

namespace iris {

/// Creates a sender that yields @p val.
inline auto just(IrisValue val) {
    return stdexec::just(std::move(val));
}

/// Returns a pipe adaptor: feeds the upstream IrisValue through @p backend
/// via a synchronous emit+recv round-trip.
template<Backend B>
auto via(B& backend) {
    return stdexec::then([&backend](IrisValue val) -> IrisValue {
        backend.emit(std::move(val));
        return backend.recv();
    });
}

/// Re-export stdexec::then for arbitrary IrisValue transforms.
using stdexec::then;

/// Thread pool backed by exec::static_thread_pool.
class thread_pool {
    exec::static_thread_pool pool_;
public:
    explicit thread_pool(unsigned n = std::thread::hardware_concurrency())
        : pool_(n) {}
    auto scheduler() noexcept { return pool_.get_scheduler(); }
};

/// Returns a pipe adaptor that transfers execution to @p tp's scheduler.
/// Operations chained after this run on the thread pool.
inline auto schedule_on(thread_pool& tp) {
    return stdexec::continues_on(tp.scheduler());
}

/// Runs @p s synchronously. Returns the IrisValue, or std::nullopt on error.
template<stdexec::sender S>
std::optional<IrisValue> sync_wait(S s) {
    if (auto r = stdexec::sync_wait(std::move(s)))
        return std::get<0>(std::move(*r));
    return std::nullopt;
}

} // namespace iris
