// SPDX-License-Identifier: MIT
/// @file   sdk/iris_backend.h
/// @brief  Iris C ABI — loadable via dlopen, zero C++ dependency.
///
/// This is the public interface (MIT). Consumers call iris_backend_java_create(),
/// interact through the vtable, then call iris_backend_destroy(). No C++ headers
/// required on the consumer side.

#pragma once

#include <sdk/iris_registry.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iris_backend_s iris_backend_t;

typedef struct {
    /// Connect to the runtime. Returns 0 on success, non-zero on error.
    int    (*connect)(iris_backend_t* self, const char* classpath);
    /// Disconnect and release runtime resources.
    void   (*disconnect)(iris_backend_t* self);
    /// Push a typed value. type_id identifies the type; payload is raw bytes.
    void   (*emit)(iris_backend_t* self,
                   uint64_t type_id, const uint8_t* payload, size_t payload_size);
    /// Pop the next typed value. Fills type_id_out and payload_out (up to cap bytes).
    /// Returns bytes written into payload_out, or 0 if the queue is empty.
    size_t (*recv)(iris_backend_t* self,
                   uint64_t* type_id_out, uint8_t* payload_out, size_t cap);
} iris_backend_vtable_t;

struct iris_backend_s {
    const iris_backend_vtable_t* vtable;
    void*                        impl;
    /// Called by iris_backend_destroy() to free impl. Set by each factory.
    void                       (*_destroy_impl)(void* impl);
};

/// Create a JavaBackend handle. The caller owns it; free with iris_backend_destroy().
iris_backend_t* iris_backend_java_create(void);
/// Destroy a handle created by any iris_backend_*_create function.
void            iris_backend_destroy(iris_backend_t* handle);

#ifdef __cplusplus
}
#endif
