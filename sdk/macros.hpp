// SPDX-License-Identifier: MIT
/// @file   sdk/macros.hpp
/// @brief  IRIS_TYPE / IRIS_FIELD macros and typed wrap/unwrap helpers.
///
/// Everything here is MIT — include in any project to describe types and
/// move values into the Iris pipeline without copyleft obligation.

#pragma once

#include <sdk/types.hpp>
#include <value.hpp>
#include <registry.hpp>
#include <cstddef>
#include <cstring>

// ── Field description macros ─────────────────────────────────────────────────

/// Describe one field of @p struct_t for use inside IRIS_TYPE.
#define IRIS_FIELD(struct_t, field)                                 \
    ::iris::FieldDesc {                                             \
        #field,                                                     \
        ::iris::primitive_kind<decltype(struct_t::field)>(),        \
        offsetof(struct_t, field),                                  \
        sizeof(((struct_t*)nullptr)->field),                        \
        ""                                                          \
    }

/// Like IRIS_FIELD but overrides the Java field name with @p jni_name_str.
#define IRIS_FIELD_NAMED(struct_t, field, jni_name_str)             \
    ::iris::FieldDesc {                                             \
        #field,                                                     \
        ::iris::primitive_kind<decltype(struct_t::field)>(),        \
        offsetof(struct_t, field),                                  \
        sizeof(((struct_t*)nullptr)->field),                        \
        jni_name_str                                                \
    }

/// Like IRIS_FIELD but forces PrimitiveKind::Bytes — use for fixed-size char
/// arrays and any member type not covered by primitive_kind<>.
#define IRIS_BYTES_FIELD(struct_t, field)                               \
    ::iris::FieldDesc {                                                 \
        #field,                                                         \
        ::iris::PrimitiveKind::Bytes,                                   \
        offsetof(struct_t, field),                                      \
        sizeof(((struct_t*)nullptr)->field),                            \
        ""                                                              \
    }

// ── Type registration macro ───────────────────────────────────────────────────

/// Register @p T with the global TypeRegistry at static-init time.
/// Also specialises iris::type_id_of<T>() so the TypeId is retrievable
/// by type without going through the registry by name.
#define IRIS_TYPE(T, ...)                                               \
    namespace iris_reg {                                                \
    static const ::iris::TypeId _iris_id_##T = [] {                    \
        return ::iris::TypeRegistry::global().register_type(           \
            ::iris::TypeDescriptor{.id=0, .name=#T,                    \
                .total_size=sizeof(T), .fields={__VA_ARGS__}}          \
        );                                                              \
    }();                                                                \
    }                                                                   \
    namespace iris {                                                    \
    template<> inline TypeId type_id_of<T>() {                         \
        return ::iris_reg::_iris_id_##T;                               \
    }                                                                   \
    }

// ── Typed helpers ─────────────────────────────────────────────────────────────

namespace iris {

/// Copy @p value into an IrisValue raw-bytes payload.
template<typename T>
IrisValue wrap(const T& value) {
    IrisValue v;
    v.type_id     = type_id_of<T>();
    auto bytes    = std::vector<std::byte>(sizeof(T));
    std::memcpy(bytes.data(), &value, sizeof(T));
    v.payload     = std::move(bytes);
    return v;
}

/// Reinterpret the raw-bytes payload of @p v as a const T reference.
/// The caller is responsible for ensuring v holds a raw payload of the
/// correct type and size — no bounds check is performed.
template<typename T>
const T& unwrap(const IrisValue& v) {
    return *reinterpret_cast<const T*>(v.raw().data());
}

} // namespace iris
