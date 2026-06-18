// SPDX-License-Identifier: MIT
/// @file   sdk/cpp/macros.hpp
/// @brief  IRIS_TYPE / IRIS_FIELD macros and typed wrap/unwrap helpers.
///
/// IRIS_TYPE uses the C ABI (sdk/iris_registry.h) for type registration —
/// no GPL registry header required. wrap<T>/unwrap<T> require value.hpp.

#pragma once

#include <sdk/cpp/types.hpp>
#include <sdk/iris_registry.h>
#include <value.hpp>
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

/// Register @p T with the global TypeRegistry at static-init time via the
/// C ABI (no GPL headers required here). Also specialises iris::type_id_of<T>().
#define IRIS_TYPE(T, ...)                                                       \
    namespace iris_reg {                                                        \
    static const ::iris::TypeId _iris_id_##T = [] {                            \
        const ::iris::FieldDesc _f[] = {__VA_ARGS__};                          \
        constexpr size_t _n = sizeof(_f) / sizeof(_f[0]);                      \
        ::iris_field_t _cf[_n];                                                 \
        for (size_t _i = 0; _i < _n; ++_i)                                     \
            _cf[_i] = { _f[_i].name.c_str(),                                   \
                        static_cast<uint8_t>(_f[_i].kind),                     \
                        static_cast<uint32_t>(_f[_i].offset),                  \
                        static_cast<uint32_t>(_f[_i].size),                    \
                        _f[_i].jni_name.empty() ?                               \
                            nullptr : _f[_i].jni_name.c_str() };               \
        return static_cast<::iris::TypeId>(                                     \
            ::iris_type_register(#T, _cf, _n, sizeof(T)));                     \
    }();                                                                        \
    }                                                                           \
    namespace iris {                                                            \
    template<> inline TypeId type_id_of<T>() {                                 \
        return ::iris_reg::_iris_id_##T;                                        \
    }                                                                           \
    }

// ── Typed helpers ─────────────────────────────────────────────────────────────

namespace iris {

/// Copy @p value into an IrisValue raw-bytes payload.
template<typename T>
IrisValue wrap(const T& value) {
    IrisValue v;
    v.type_id = type_id_of<T>();
    v.payload = IrisBuffer::from(&value, sizeof(T));
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
