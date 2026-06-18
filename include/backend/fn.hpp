/// @file   include/backend/fn.hpp
/// @brief  FnBackend<F> and PipedFn<F1,F2> — callable and composed backends.
///
/// FnBackend wraps any callable F : IrisValue(IrisValue&&).
/// PipedFn chains two FnBackends via operator|: emit() feeds F1, recv()
/// returns F2(F1(input)) — zero-copy IrisBuffer passes through shared_ptr.

#pragma once

#include <backend.hpp>
#include <channel.hpp>
#include <functional>

namespace iris {

template<typename F>
class FnBackend {
    F        fn_;
    Channel* in_  = nullptr;
    Channel* out_ = nullptr;

public:
    explicit FnBackend(F f) : fn_(std::move(f)) {}

    void set_input(Channel* ch)  { in_  = ch; }
    void set_output(Channel* ch) { out_ = ch; }

    std::string_view runtime_name() const { return "fn"; }
    bool can_handle(const TypeDescriptor&) const { return true; }

    void emit(IrisValue&& v) { if (out_) out_->push(std::move(v)); }

    IrisValue recv() {
        if (!in_) return {};
        return fn_(in_->pop());
    }
};

static_assert(Backend<FnBackend<std::function<IrisValue(IrisValue&&)>>>);

// ── Pipe composition ──────────────────────────────────────────────────────────

/// Two chained FnBackends. emit() feeds F1; recv() returns F2(F1(input)).
/// IrisBuffer passes as shared_ptr — no copy between the two stages.
template<typename F1, typename F2>
class PipedFn {
    FnBackend<F1> a_;
    FnBackend<F2> b_;
    Channel       in_link_;   ///< external emit() → a's input
    Channel       mid_link_;  ///< a's output → b's input

public:
    PipedFn(FnBackend<F1> a, FnBackend<F2> b)
        : a_(std::move(a)), b_(std::move(b)) {
        a_.set_input(&in_link_);
        b_.set_input(&mid_link_);
    }

    // Channel owns a mutex — PipedFn is non-movable; FnBackend pointers stay valid.
    PipedFn(PipedFn&&)            = delete;
    PipedFn& operator=(PipedFn&&) = delete;

    std::string_view runtime_name() const { return "pipe"; }
    bool can_handle(const TypeDescriptor& td) const { return a_.can_handle(td); }

    void emit(IrisValue&& v) { in_link_.push(std::move(v)); }

    IrisValue recv() {
        IrisValue r = a_.recv();       // pops in_link_, applies F1
        mid_link_.push(std::move(r));
        return b_.recv();              // pops mid_link_, applies F2
    }
};

template<typename F1, typename F2>
PipedFn<F1, F2> operator|(FnBackend<F1> a, FnBackend<F2> b) {
    return PipedFn<F1, F2>(std::move(a), std::move(b));
}

static_assert(Backend<PipedFn<std::function<IrisValue(IrisValue&&)>,
                               std::function<IrisValue(IrisValue&&)>>>);

} // namespace iris
