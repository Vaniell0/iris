/// @file   include/fn_backend.hpp
/// @brief  FnBackend<F> — wraps any callable as a Backend.
///
/// F must be callable as IrisValue(IrisValue&&). recv() pops from the input
/// Channel, applies F, and returns the result. emit() pushes to the output
/// Channel unchanged.

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

    // ── Backend concept ───────────────────────────────────────────────────────

    std::string_view runtime_name() const { return "fn"; }

    bool can_handle(const TypeDescriptor&) const { return true; }

    void emit(IrisValue&& v) {
        if (out_) out_->push(std::move(v));
    }

    IrisValue recv() {
        if (!in_) return {};
        return fn_(in_->pop());
    }
};

static_assert(Backend<FnBackend<std::function<IrisValue(IrisValue&&)>>>);

} // namespace iris
