/// @file   include/value.hpp
/// @brief  OpaqueHandle (backend-owned RAII) and IrisValue (universal pipeline container).

#pragma once

#include <types.hpp>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace iris {

/// Type-erased RAII handle for any backend-managed object (JVM global ref,
/// Node.js Persistent, Python PyObject*, etc.). The backend sets ptr/ctx/release
/// at creation time; the core owns the lifetime without knowing what ptr is.
struct OpaqueHandle {
    void* ptr     = nullptr;
    void* ctx     = nullptr;
    void (*release)(void* ptr, void* ctx) = nullptr;

    OpaqueHandle() = default;

    OpaqueHandle(void* p, void* c, void (*r)(void*, void*))
        : ptr(p), ctx(c), release(r) {}

    ~OpaqueHandle() {
        if (release && ptr) release(ptr, ctx);
    }

    OpaqueHandle(OpaqueHandle&& o) noexcept
        : ptr(o.ptr), ctx(o.ctx), release(o.release) {
        o.ptr = nullptr;
    }

    OpaqueHandle& operator=(OpaqueHandle&& o) noexcept {
        std::swap(ptr,     o.ptr);
        std::swap(ctx,     o.ctx);
        std::swap(release, o.release);
        return *this;
    }

    OpaqueHandle(const OpaqueHandle&)            = delete;
    OpaqueHandle& operator=(const OpaqueHandle&) = delete;
};

/// Universal pipeline container — exactly one representation is active at a time.
/// Conversion between representations is lazy and driven by the TypeDescriptor.
struct IrisValue {
    TypeId type_id = 0;

    std::variant<
        std::vector<std::byte>, ///< C struct raw bytes — layout from TypeDescriptor
        OpaqueHandle,           ///< Backend-owned object (JVM, Node, etc.)
        std::string             ///< String fallback — always available via to_string()
    > payload;

    bool is_raw()    const { return std::holds_alternative<std::vector<std::byte>>(payload); }
    bool is_opaque() const { return std::holds_alternative<OpaqueHandle>(payload); }
    bool is_str()    const { return std::holds_alternative<std::string>(payload); }

    std::vector<std::byte>&       raw()        { return std::get<std::vector<std::byte>>(payload); }
    const std::vector<std::byte>& raw()  const { return std::get<std::vector<std::byte>>(payload); }
    OpaqueHandle&                 opaque()     { return std::get<OpaqueHandle>(payload); }
    const OpaqueHandle&           opaque()const{ return std::get<OpaqueHandle>(payload); }
};

/// Structured error returned by bridge operations instead of exceptions.
enum class IrisError {
    TypeNotFound,
    FieldMissing,
    JniException,
    JniClassNotFound,
    JniMethodNotFound,
    SizeMismatch,
    UnsupportedKind,
};

} // namespace iris
