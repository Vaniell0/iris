// SPDX-License-Identifier: MIT
/// @file   sdk/iris_registry.h
/// @brief  Iris type-registry C ABI — usable from any language via FFI.
///
/// Provides a pure-C interface to register types with the global TypeRegistry,
/// compute content-addressed TypeIds, and look up types by name.
/// No C++ headers required.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Scalar kinds mirroring iris::PrimitiveKind — guaranteed ABI-stable values.
typedef enum {
    IRIS_KIND_VOID  = 0,
    IRIS_KIND_BOOL  = 1,
    IRIS_KIND_I8    = 2,
    IRIS_KIND_I16   = 3,
    IRIS_KIND_I32   = 4,
    IRIS_KIND_I64   = 5,
    IRIS_KIND_F32   = 6,
    IRIS_KIND_F64   = 7,
    IRIS_KIND_STR   = 8,
    IRIS_KIND_BYTES = 9
} iris_kind_t;

/// One field descriptor for iris_type_register().
typedef struct {
    const char* name;       ///< C field name (null-terminated)
    uint8_t     kind;       ///< iris_kind_t value
    uint32_t    offset;     ///< byte offset within the C struct
    uint32_t    size;       ///< byte size of the field
    const char* jni_name;   ///< Java field name override; NULL → use name
} iris_field_t;

/// Compute a content-addressed FNV-64 TypeId without touching the registry.
/// Result depends only on name + field layout — identical across processes.
uint64_t iris_type_id_compute(const char*         name,
                               const iris_field_t* fields,
                               size_t              n_fields);

/// Register a type with the global TypeRegistry.
/// Returns the TypeId on success, 0 if the registry is frozen.
uint64_t iris_type_register(const char*         name,
                             const iris_field_t* fields,
                             size_t              n_fields,
                             size_t              total_size);

/// Look up a TypeId by canonical type name.
/// Returns 0 if the type has not been registered yet.
uint64_t iris_type_find_by_name(const char* name);

#ifdef __cplusplus
} // extern "C"
#endif
