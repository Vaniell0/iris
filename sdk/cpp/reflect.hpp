// SPDX-License-Identifier: MIT
/// @file   sdk/cpp/reflect.hpp
/// @brief  IRIS_REFLECT macro — auto-derives TypeDescriptor via P2996 std::meta.
///
/// Requires: cmake -DIRIS_STDMETA=ON (GCC 16+, -std=c++26, -freflection).
/// IRIS_TYPE(T, ...) still works everywhere for explicit fields or JNI overrides.

#pragma once
#ifndef IRIS_STDMETA
#  error "reflect.hpp requires -DIRIS_STDMETA=ON (GCC 16+, C++26, -freflection — see flake.nix)"
#endif

#include <meta>
#include <sdk/cpp/macros.hpp>
#include <sdk/cpp/types.hpp>
#include <sdk/iris_registry.h>
#include <type_traits>
#include <vector>

namespace iris::detail {

/// Like primitive_kind<T>() but falls back to Bytes for unrecognised types
/// (e.g. fixed-size arrays) instead of producing a linker error.
template<typename T>
consteval PrimitiveKind kind_of() {
    using U = std::remove_cv_t<T>;
    if constexpr      (std::is_same_v<U, bool>)    return PrimitiveKind::Bool;
    else if constexpr (std::is_same_v<U, int8_t>)  return PrimitiveKind::I8;
    else if constexpr (std::is_same_v<U, int16_t>) return PrimitiveKind::I16;
    else if constexpr (std::is_same_v<U, int32_t>) return PrimitiveKind::I32;
    else if constexpr (std::is_same_v<U, int64_t>) return PrimitiveKind::I64;
    else if constexpr (std::is_same_v<U, float>)   return PrimitiveKind::F32;
    else if constexpr (std::is_same_v<U, double>)  return PrimitiveKind::F64;
    else if constexpr (std::is_array_v<U> &&
                       std::is_same_v<std::remove_extent_t<U>, char>)
                                                    return PrimitiveKind::CStr;
    else                                            return PrimitiveKind::Bytes;
}

/// Build a FieldDesc vector by iterating T's non-static data members via
/// P2996 expansion statement. Runs at static-init time.
template<typename T>
std::vector<FieldDesc> reflect_fields() {
    std::vector<FieldDesc> result;
    template for (constexpr auto M : std::meta::nonstatic_data_members_of(^T)) {
        using FT = [: std::meta::type_of(M) :];
        result.push_back(FieldDesc{
            std::string(std::meta::identifier_of(M)),
            kind_of<FT>(),
            static_cast<size_t>(std::meta::offset_of(M).bytes),
            sizeof(FT),
            ""
        });
    }
    return result;
}

} // namespace iris::detail

// ── Macro ─────────────────────────────────────────────────────────────────────

/// Register @p T with the global TypeRegistry by reflecting its non-static data
/// members — no field list required. Also specialises iris::type_id_of<T>().
///
/// Constraints identical to IRIS_TYPE: T must be trivially copyable and have
/// standard layout. Fields of unrecognised type map to PrimitiveKind::Bytes.
#define IRIS_REFLECT(T)                                                              \
    static_assert(std::is_trivially_copyable_v<T>,                                  \
        #T ": IRIS_REFLECT requires trivially copyable");                            \
    static_assert(std::is_standard_layout_v<T>,                                     \
        #T ": IRIS_REFLECT requires standard layout");                               \
    namespace iris_reg {                                                             \
    static const ::iris::TypeId _iris_id_##T = [] {                                 \
        const auto _fields = ::iris::detail::reflect_fields<T>();                   \
        std::vector<::iris_field_t> _cf(_fields.size());                             \
        for (size_t _i = 0; _i < _fields.size(); ++_i)                              \
            _cf[_i] = { _fields[_i].name.c_str(),                                   \
                        static_cast<uint8_t>(_fields[_i].kind),                     \
                        static_cast<uint32_t>(_fields[_i].offset),                  \
                        static_cast<uint32_t>(_fields[_i].size),                    \
                        nullptr };                                                   \
        return static_cast<::iris::TypeId>(                                          \
            ::iris_type_register(#T, _cf.data(), _cf.size(), sizeof(T)));           \
    }();                                                                             \
    }                                                                                \
    namespace iris {                                                                 \
    template<> inline TypeId type_id_of<T>() {                                      \
        return ::iris_reg::_iris_id_##T;                                             \
    }                                                                                \
    }
