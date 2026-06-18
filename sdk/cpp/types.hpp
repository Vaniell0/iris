// SPDX-License-Identifier: MIT
/// @file   sdk/cpp/types.hpp
/// @brief  Core IR types — public C++ contract between Iris and its users.
///
/// PrimitiveKind, FieldDesc, TypeId, TypeDescriptor, and the FNV-64 hash
/// used for content-addressed type identity. This header has no dependency
/// on the Iris core library — include it in any runtime or binding without
/// pulling in the registry or value machinery.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace iris {

/// Primitive scalar kinds understood by Iris across all backends.
enum class PrimitiveKind : uint8_t {
    Void, Bool, I8, I16, I32, I64, F32, F64, Str, Bytes
};

/// One field within a TypeDescriptor.
struct FieldDesc {
    std::string   name;
    PrimitiveKind kind;
    size_t        offset;
    size_t        size;
    /// When non-empty, overrides `name` as the Java field identifier.
    std::string   jni_name;

    const std::string& effective_jni_name() const {
        return jni_name.empty() ? name : jni_name;
    }
};

/// Content-addressed type identity: FNV-64 hash of name + field layout.
/// Identical layout across processes and builds produces an identical TypeId.
using TypeId = uint64_t;

constexpr TypeId fnv64_basis = 14695981039346656037ULL;
constexpr TypeId fnv64_prime = 1099511628211ULL;

inline TypeId fnv64(std::string_view s, TypeId h = fnv64_basis) noexcept {
    for (unsigned char c : s) { h ^= c; h *= fnv64_prime; }
    return h;
}

inline TypeId compute_type_id(std::string_view name,
                               std::span<const FieldDesc> fields) noexcept {
    TypeId h = fnv64(name);
    for (auto& f : fields) {
        h = fnv64(f.name, h);
        h ^= static_cast<uint8_t>(f.kind);
        h *= fnv64_prime;
        h ^= f.size;
        h *= fnv64_prime;
    }
    return h;
}

/// Runtime description of a type produced by all registration paths.
/// JNI handles live here but are populated lazily by JavaBackend — the
/// registry itself never touches them.
struct TypeDescriptor {
    TypeId                      id         = 0;
    std::string                 name;
    size_t                      total_size = 0;
    std::vector<FieldDesc>      fields;

    /// jclass global ref — set by JavaBackend::ensure_handles.
    mutable void*               java_class = nullptr;
    /// jmethodID of the no-arg constructor — set by JavaBackend::ensure_handles.
    mutable void*               java_ctor  = nullptr;
    /// Per-field jfieldID, indexed parallel to `fields`.
    mutable std::vector<void*>  java_fields = {};

    const FieldDesc* find_field(std::string_view n) const {
        for (auto& f : fields) if (f.name == n) return &f;
        return nullptr;
    }
};

/// Map C++ scalar types to their PrimitiveKind at compile time.
template<typename T> constexpr PrimitiveKind primitive_kind();
template<> constexpr PrimitiveKind primitive_kind<bool>()    { return PrimitiveKind::Bool; }
template<> constexpr PrimitiveKind primitive_kind<int8_t>()  { return PrimitiveKind::I8;   }
template<> constexpr PrimitiveKind primitive_kind<int16_t>() { return PrimitiveKind::I16;  }
template<> constexpr PrimitiveKind primitive_kind<int32_t>() { return PrimitiveKind::I32;  }
template<> constexpr PrimitiveKind primitive_kind<int64_t>() { return PrimitiveKind::I64;  }
template<> constexpr PrimitiveKind primitive_kind<float>()   { return PrimitiveKind::F32;  }
template<> constexpr PrimitiveKind primitive_kind<double>()  { return PrimitiveKind::F64;  }

/// Forward declaration used by the IRIS_TYPE macro in sdk/cpp/macros.hpp.
template<typename T> TypeId type_id_of();

} // namespace iris
