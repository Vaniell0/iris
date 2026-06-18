/// @file   src/iris_backend_handle.cpp
/// @brief  C ABI implementation wrapping JavaBackend.

#include <iris_backend_handle.h>
#include <backend/java/java_backend.hpp>
#include <channel.hpp>
#include <cstring>
#include <new>

namespace {

struct Impl {
    iris::JavaBackend backend;
    iris::Channel     queue;   ///< emit() pushes here; recv() pops here
};

int impl_connect(iris_backend_t* self, const char* classpath) {
    auto* impl = static_cast<Impl*>(self->impl);
    auto r = impl->backend.connect(classpath);
    return r.has_value() ? 0 : -1;
}

void impl_disconnect(iris_backend_t* self) {
    auto* impl = static_cast<Impl*>(self->impl);
    impl->backend.disconnect();
}

void impl_emit(iris_backend_t* self,
               uint64_t type_id, const uint8_t* payload, size_t payload_size) {
    auto* impl = static_cast<Impl*>(self->impl);

    iris::IrisValue v;
    v.type_id = static_cast<iris::TypeId>(type_id);
    std::vector<std::byte> raw(payload_size);
    std::memcpy(raw.data(), payload, payload_size);
    v.payload = std::move(raw);

    impl->queue.push(std::move(v));
}

size_t impl_recv(iris_backend_t* self,
                 uint64_t* type_id_out, uint8_t* payload_out, size_t cap) {
    auto* impl = static_cast<Impl*>(self->impl);

    auto opt = impl->queue.try_pop();
    if (!opt) return 0;

    iris::IrisValue v = std::move(*opt);
    *type_id_out = static_cast<uint64_t>(v.type_id);

    if (!v.is_raw()) return 0;
    const auto& raw = v.raw();
    size_t n = raw.size() < cap ? raw.size() : cap;
    std::memcpy(payload_out, raw.data(), n);
    return n;
}

static const iris_backend_vtable_t kJavaVtable = {
    .connect    = impl_connect,
    .disconnect = impl_disconnect,
    .emit       = impl_emit,
    .recv       = impl_recv,
};

} // namespace

extern "C" {

iris_backend_t* iris_backend_java_create(void) {
    auto* handle = new (std::nothrow) iris_backend_t;
    if (!handle) return nullptr;
    auto* impl = new (std::nothrow) Impl;
    if (!impl) { delete handle; return nullptr; }
    handle->vtable = &kJavaVtable;
    handle->impl   = impl;
    return handle;
}

void iris_backend_destroy(iris_backend_t* handle) {
    if (!handle) return;
    delete static_cast<Impl*>(handle->impl);
    delete handle;
}

} // extern "C"
