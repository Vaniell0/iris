/// @file   include/registry.hpp
/// @brief  TypeRegistry — the live catalog of all known types.
///
/// Two registration paths both produce an identical TypeDescriptor:
/// static (IRIS_TYPE macro at compile time) and dynamic (from_fields,
/// called by any backend or script parser at runtime). JVM-specific
/// reflection lives in JavaBackend::register_class(), not here.
/// Thread-safe: concurrent reads share a lock, writes are exclusive.

#pragma once

#include <sdk/cpp/types.hpp>
#include <value.hpp>
#include <atomic>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace iris {

// ── Inspection types ─────────────────────────────────────────────────────────

/// Static description of one field as seen from outside — no JNI required.
struct FieldInspection {
    std::string   name;
    PrimitiveKind kind;
    size_t        offset;
    size_t        size;
    std::string   jni_sig;  ///< JNI type descriptor ("I", "J", "Z", …)
};

/// Full structural snapshot of a registered type — safe to read at any time.
struct TypeInspection {
    TypeId                       id;
    std::string                  name;
    size_t                       total_size;
    std::vector<FieldInspection> fields;
    bool java_handles_cached;  ///< true if a backend has already resolved jclass
};

// ── TypeRegistry ─────────────────────────────────────────────────────────────

class TypeRegistry {
    std::unordered_map<TypeId, TypeDescriptor>  types_;
    std::unordered_map<std::string, TypeId>     by_name_;
    mutable std::shared_mutex                   mu_;
    std::atomic<bool>                           frozen_{false};

public:
    static TypeRegistry& global();

    /// Path 1 — called by IRIS_TYPE at static-init time.
    /// Returns 0 if the registry is frozen.
    TypeId register_type(TypeDescriptor desc);

    /// Path 2 — explicit field list from a backend or script parser.
    /// Returns 0 if the registry is frozen.
    TypeId from_fields(std::string_view name, std::vector<FieldDesc> fields,
                       size_t total_size = 0);

    const TypeDescriptor* find(TypeId id)          const;
    const TypeDescriptor* find(std::string_view n) const;
    bool                  contains(TypeId id)      const;

    // ── Inspection ────────────────────────────────────────────────────────────

    /// Build a structural snapshot of a type without touching any runtime.
    /// Returns std::nullopt if the TypeId is not registered.
    std::optional<TypeInspection> inspect(TypeId id) const;

    // ── Freeze ────────────────────────────────────────────────────────────────

    /// Lock the registry. Any registration attempt after this point returns 0.
    void freeze();
    bool is_frozen() const { return frozen_.load(std::memory_order_relaxed); }
};

} // namespace iris
