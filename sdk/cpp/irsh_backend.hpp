// SPDX-License-Identifier: MIT
/// @file   sdk/cpp/irsh_backend.hpp
/// @brief  C++ wrapper for irsh plugin backend authors.
///
/// Inherit IrshPlugin, override check() and make_gen(), then place
/// IRIS_IRSH_BACKEND(YourClass) in one .cpp file to export the required symbol.
///
/// Example:
///   class MyPlugin : public iris::irsh::sdk::IrshPlugin {
///       const char* name() const noexcept override { return "myplugin"; }
///       iris_irtype_t check(...) const override { ... }
///       iris_gen_handle_t* make_gen(...) override { ... }
///   };
///   IRIS_IRSH_BACKEND(MyPlugin)

#pragma once

#include <sdk/irsh_backend.h>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace iris::irsh::sdk {

// ── ValueView — non-owning view of an iris_irvalue_c_t ───────────────────────

struct ValueView {
    uint64_t       type_id       = 0;
    const uint8_t* payload       = nullptr;
    size_t         payload_size  = 0;
};

// ── UpstreamGen — owning RAII wrapper around iris_gen_handle_t* ───────────────

class UpstreamGen {
    iris_gen_handle_t* h_;
public:
    explicit UpstreamGen(iris_gen_handle_t* h) noexcept : h_(h) {}
    ~UpstreamGen() { if (h_) h_->vtable->destroy(h_); h_ = nullptr; }

    UpstreamGen(const UpstreamGen&)            = delete;
    UpstreamGen& operator=(const UpstreamGen&) = delete;
    UpstreamGen(UpstreamGen&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }

    std::optional<ValueView> operator()() const noexcept {
        if (!h_) return std::nullopt;
        const auto* v = h_->vtable->next(h_);
        if (!v) return std::nullopt;
        return ValueView{v->type_id, v->payload, v->payload_size};
    }
};

// ── make_plugin_gen — build a gen handle from a pull lambda ──────────────────

/// Build an iris_gen_handle_t* backed by any std::function<optional<ValueView>()>.
/// The returned handle owns the function; call destroy() to release it.
inline iris_gen_handle_t* make_plugin_gen(
    std::function<std::optional<ValueView>()> pull)
{
    struct Impl {
        std::function<std::optional<ValueView>()> pull;
        iris_irvalue_c_t   current{};
        std::vector<uint8_t> buf;
    };

    static const iris_gen_vtable_t vtable = {
        [](iris_gen_handle_t* h) -> const iris_irvalue_c_t* {
            auto* impl = static_cast<Impl*>(h->impl);
            auto v = impl->pull();
            if (!v) return nullptr;
            impl->buf.assign(v->payload, v->payload + v->payload_size);
            impl->current = {v->type_id, impl->buf.data(), impl->buf.size()};
            return &impl->current;
        },
        [](iris_gen_handle_t* h) {
            delete static_cast<Impl*>(h->impl);
            delete h;
        },
    };

    auto* impl = new Impl{std::move(pull)};
    auto* h    = new iris_gen_handle_t;
    h->vtable  = &vtable;
    h->impl    = impl;
    return h;
}

/// Wrap upstream in a new gen that applies fn(UpstreamGen&) per element.
/// Upstream is destroyed when the returned handle is destroyed.
template<typename Fn>
iris_gen_handle_t* transform_gen(iris_gen_handle_t* upstream, Fn fn) {
    auto up = std::make_shared<UpstreamGen>(upstream);
    return make_plugin_gen([up, fn = std::move(fn)]() mutable
                           -> std::optional<ValueView> { return fn(*up); });
}

// ── IrshPlugin — C++ base class for plugin authors ────────────────────────────

class IrshPlugin {
public:
    virtual ~IrshPlugin() = default;

    virtual const char* name() const noexcept = 0;

    virtual int verify() const noexcept { return 0; }

    virtual iris_irtype_t check(
        std::string_view               op,
        const iris_backend_config_c_t& config,
        iris_irtype_t                  input,
        void (*emit_error)(void*, uint32_t, uint32_t, const char*),
        void*                          error_ctx) const = 0;

    virtual iris_gen_handle_t* make_gen(
        std::string_view               op,
        const iris_backend_config_c_t& config,
        iris_gen_handle_t*             upstream) = 0;

    // ── C ABI adapters (used by make_handle) ─────────────────────────────────

    static iris_irsh_backend_t* make_handle(IrshPlugin* plugin) {
        static const iris_irsh_backend_vtable_t vtbl = {
            sizeof(iris_irsh_backend_vtable_t),
            [](const iris_irsh_backend_t* s) noexcept -> const char* {
                return static_cast<IrshPlugin*>(s->impl)->name();
            },
            [](const iris_irsh_backend_t* s) noexcept -> int {
                return static_cast<IrshPlugin*>(s->impl)->verify();
            },
            [](const iris_irsh_backend_t* s, const char* op,
               const iris_backend_config_c_t* cfg, iris_irtype_t in,
               void (*emit)(void*, uint32_t, uint32_t, const char*),
               void* ctx) -> iris_irtype_t {
                return static_cast<IrshPlugin*>(s->impl)->check(op, *cfg, in, emit, ctx);
            },
            [](iris_irsh_backend_t* s, const char* op,
               const iris_backend_config_c_t* cfg,
               iris_gen_handle_t* up) -> iris_gen_handle_t* {
                return static_cast<IrshPlugin*>(s->impl)->make_gen(op, *cfg, up);
            },
            [](iris_irsh_backend_t* s) noexcept {
                delete static_cast<IrshPlugin*>(s->impl);
                delete s;
            },
        };
        auto* h  = new iris_irsh_backend_t;
        h->vtable = &vtbl;
        h->impl   = plugin;
        return h;
    }
};

} // namespace iris::irsh::sdk

// ── Export macro ──────────────────────────────────────────────────────────────

/// Place once in a .cpp file:  IRIS_IRSH_BACKEND(MyPluginClass)
/// PluginClass must be default-constructible.
#define IRIS_IRSH_BACKEND(PluginClass)                                         \
    extern "C" iris_irsh_backend_t* iris_irsh_backend_create() {              \
        return ::iris::irsh::sdk::IrshPlugin::make_handle(new PluginClass);   \
    }
