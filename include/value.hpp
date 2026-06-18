/// @file   include/value.hpp
/// @brief  IrisBuffer (zero-copy ref-counted), OpaqueHandle, and IrisValue.

#pragma once

#include <sdk/cpp/types.hpp>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace iris {

// ── IrisBuffer ────────────────────────────────────────────────────────────────

/// Zero-copy ref-counted byte buffer.
/// Passes between backends by shared_ptr copy — no memcpy in the pipeline.
struct IrisBuffer {
    std::shared_ptr<std::byte[]> data_;
    std::size_t size_   = 0;
    std::size_t offset_ = 0;

    IrisBuffer() = default;

    /// Allocate n zero-initialised bytes.
    static IrisBuffer alloc(std::size_t n) {
        IrisBuffer b;
        b.data_   = std::make_shared<std::byte[]>(n); // value-init = zeroed
        b.size_   = n;
        b.offset_ = 0;
        return b;
    }

    /// Copy src into a fresh allocation (one-time copy; zero-copy from here on).
    static IrisBuffer from(const void* src, std::size_t n) {
        IrisBuffer b = alloc(n);
        if (n > 0 && src) std::memcpy(b.data_.get(), src, n);
        return b;
    }

    const std::byte* data() const noexcept { return data_.get() + offset_; }
    std::byte*       data()       noexcept { return data_.get() + offset_; }
    std::size_t      size() const noexcept { return size_;                  }

    /// Return a sub-view sharing the same backing allocation — zero copy.
    IrisBuffer slice(std::size_t off, std::size_t len) const {
        IrisBuffer b;
        b.data_   = data_;
        b.size_   = len;
        b.offset_ = offset_ + off;
        return b;
    }

    bool empty() const noexcept { return size_ == 0; }
};

// ── OpaqueHandle ──────────────────────────────────────────────────────────────

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

// ── IrisValue ─────────────────────────────────────────────────────────────────

/// Universal pipeline container — exactly one representation is active at a time.
/// Conversion between representations is lazy and driven by the TypeDescriptor.
struct IrisValue {
    TypeId type_id = 0;

    std::variant<
        IrisBuffer,   ///< Zero-copy ref-counted raw bytes (C struct layout)
        OpaqueHandle, ///< Backend-owned object (JVM, Node, etc.)
        std::string   ///< String fallback — always available via to_string()
    > payload;

    bool is_raw()    const { return std::holds_alternative<IrisBuffer>(payload); }
    bool is_opaque() const { return std::holds_alternative<OpaqueHandle>(payload); }
    bool is_str()    const { return std::holds_alternative<std::string>(payload); }

    IrisBuffer&       raw()        { return std::get<IrisBuffer>(payload); }
    const IrisBuffer& raw()  const { return std::get<IrisBuffer>(payload); }
    OpaqueHandle&                 opaque()     { return std::get<OpaqueHandle>(payload); }
    const OpaqueHandle&           opaque()const{ return std::get<OpaqueHandle>(payload); }
};

// ── IrisError ─────────────────────────────────────────────────────────────────

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
