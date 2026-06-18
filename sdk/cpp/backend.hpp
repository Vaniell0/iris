// SPDX-License-Identifier: MIT
/// @file   sdk/cpp/backend.hpp
/// @brief  RAII C++ wrapper around the Iris C ABI (sdk/iris_backend.h).
///
/// No GPL headers are included here — only sdk/iris_backend.h.
/// Link against libiris (and optionally libiris_java) for factory implementations.

#pragma once

#include <sdk/iris_backend.h>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace iris::sdk {

/// RAII owner of an iris_backend_t handle. Not copyable; movable.
class Backend {
    iris_backend_t* h_ = nullptr;

public:
    Backend() = default;

    /// Take ownership of a raw handle returned by an iris_backend_*_create factory.
    explicit Backend(iris_backend_t* h) noexcept : h_(h) {}

    ~Backend() { reset(); }

    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    Backend(Backend&& o) noexcept : h_(std::exchange(o.h_, nullptr)) {}
    Backend& operator=(Backend&& o) noexcept {
        if (this != &o) { reset(); h_ = std::exchange(o.h_, nullptr); }
        return *this;
    }

    /// Connect to the underlying runtime. Returns true on success.
    bool connect(const char* classpath = nullptr) {
        return h_ && h_->vtable->connect(h_, classpath) == 0;
    }

    /// Disconnect without destroying the handle (reusable after reconnect).
    void disconnect() {
        if (h_) h_->vtable->disconnect(h_);
    }

    /// Push a typed value into the backend's input queue.
    void emit(uint64_t type_id, const uint8_t* payload, std::size_t size) {
        if (h_) h_->vtable->emit(h_, type_id, payload, size);
    }

    /// Pop the next typed value. Fills type_id_out and payload_out (up to cap bytes).
    /// Returns bytes written into payload_out, or 0 if queue is empty.
    std::size_t recv(uint64_t* type_id_out, uint8_t* buf, std::size_t cap) {
        return h_ ? h_->vtable->recv(h_, type_id_out, buf, cap) : 0;
    }

    bool valid() const noexcept { return h_ != nullptr; }
    iris_backend_t* handle() const noexcept { return h_; }

private:
    void reset() {
        if (!h_) return;
        h_->vtable->disconnect(h_);
        iris_backend_destroy(h_);
        h_ = nullptr;
    }
};

/// Convenience factory: create a JavaBackend wrapped in RAII.
/// Requires libiris to be linked with IRIS_JAVA_BACKEND=ON.
inline Backend java_backend() { return Backend(iris_backend_java_create()); }

} // namespace iris::sdk
