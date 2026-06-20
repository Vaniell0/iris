/// @file src/irish/backend/plugin_loader.cpp
#include "plugin_loader.hpp"
#include "../checker/checker.hpp"
#include "../checker/irtype.hpp"
#include "../parser/ast.hpp"
#include <sdk/irsh_backend.h>
#include <value.hpp>
#include <dlfcn.h>
#include <dirent.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace iris::irsh {

namespace {

// ── Type conversion helpers ───────────────────────────────────────────────────

static iris_irtype_t to_c_irtype(const IrType& t) {
    iris_irtype_t r{};
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, VoidType>)
            r.kind = IRIS_IRTYPE_VOID;
        else if constexpr (std::is_same_v<T, AnyType>)
            r.kind = IRIS_IRTYPE_ANY;
        else if constexpr (std::is_same_v<T, ScalarType>) {
            r.kind = IRIS_IRTYPE_SCALAR;
            r.scalar_kind = static_cast<uint8_t>(v.kind);
        }
        else if constexpr (std::is_same_v<T, StructType>) {
            r.kind = IRIS_IRTYPE_STRUCT;
            r.elem_id = v.id;
        }
        else if constexpr (std::is_same_v<T, StreamType>) {
            r.kind = IRIS_IRTYPE_STREAM;
            r.elem_id = v.elem_id;
        }
        else if constexpr (std::is_same_v<T, VecType>) {
            r.kind = IRIS_IRTYPE_VEC;
            r.elem_id = v.elem_id;
        }
        else if constexpr (std::is_same_v<T, TextLineType>)
            r.kind = IRIS_IRTYPE_TEXT_LINE;
        else if constexpr (std::is_same_v<T, AliasType>)
            r.kind = IRIS_IRTYPE_ALIAS;
    }, t);
    return r;
}

static IrType from_c_irtype(iris_irtype_t r) {
    switch (static_cast<iris_irtype_kind_t>(r.kind)) {
        case IRIS_IRTYPE_ANY:       return AnyType{};
        case IRIS_IRTYPE_SCALAR:    return ScalarType{static_cast<PrimitiveKind>(r.scalar_kind)};
        case IRIS_IRTYPE_STRUCT:    return StructType{r.elem_id, {}};
        case IRIS_IRTYPE_STREAM:    return StreamType{r.elem_id};
        case IRIS_IRTYPE_VEC:       return VecType{r.elem_id};
        case IRIS_IRTYPE_TEXT_LINE: return TextLineType{};
        case IRIS_IRTYPE_ALIAS:     return AliasType{};
        default:                    return VoidType{};
    }
}

// field_ptrs must remain valid for the duration of the C call
static iris_backend_config_c_t to_c_config(const BackendConfig& cfg,
                                            std::vector<const char*>& field_ptrs) {
    iris_backend_config_c_t r{};
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            r.kind = IRIS_CONFIG_NONE;
        } else if constexpr (std::is_same_v<T, std::string>) {
            r.kind   = IRIS_CONFIG_STRING;
            r.string = v.c_str();
        } else if constexpr (std::is_same_v<T, Expr>) {
            r.kind        = IRIS_CONFIG_EXPR;
            r.expr.opaque = &v;
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            r.kind = IRIS_CONFIG_FIELD_LIST;
            field_ptrs.clear();
            for (auto& s : v) field_ptrs.push_back(s.c_str());
            r.field_list.fields = field_ptrs.data();
            r.field_list.count  = field_ptrs.size();
        } else if constexpr (std::is_same_v<T, SortArg>) {
            r.kind            = IRIS_CONFIG_SORT_ARG;
            r.sort_arg.field  = v.field.c_str();
            r.sort_arg.desc   = v.desc ? 1 : 0;
        }
    }, cfg);
    return r;
}

// ── IrisGen ↔ iris_gen_handle_t* ─────────────────────────────────────────────

// Wrap an IrisGen (C++ pull function) into an iris_gen_handle_t* for C plugins.
static iris_gen_handle_t* wrap_gen(IrisGen gen) {
    struct Impl {
        IrisGen                        gen;
        std::optional<iris::IrisValue> current  = std::nullopt;
        iris_irvalue_c_t               current_c = {};
    };

    static const iris_gen_vtable_t vtbl = {
        [](iris_gen_handle_t* h) -> const iris_irvalue_c_t* {
            auto* impl = static_cast<Impl*>(h->impl);
            auto _r = impl->gen();
            impl->current = (_r && *_r) ? std::move(*_r) : std::optional<iris::IrisValue>{};
            if (!impl->current) return nullptr;
            const iris::IrisValue& v = *impl->current;
            const uint8_t* data      = nullptr;
            size_t         size      = 0;
            if (v.is_raw()) {
                auto& buf = v.raw();
                data = reinterpret_cast<const uint8_t*>(buf.data());
                size = buf.size();
            } else if (v.is_str()) {
                auto& s = std::get<std::string>(v.payload);
                data = reinterpret_cast<const uint8_t*>(s.data());
                size = s.size();
            }
            impl->current_c = {v.type_id, data, size};
            return &impl->current_c;
        },
        [](iris_gen_handle_t* h) {
            delete static_cast<Impl*>(h->impl);
            delete h;
        },
    };

    auto* impl = new Impl{std::move(gen)};
    auto* h    = new iris_gen_handle_t;
    h->vtable  = &vtbl;
    h->impl    = impl;
    return h;
}

// Wrap an iris_gen_handle_t* (C plugin output) into an IrisGen.
// Takes ownership: destroys h when the returned IrisGen is destroyed.
static IrisGen unwrap_gen(iris_gen_handle_t* h) {
    if (!h) return {};

    struct Destroy {
        void operator()(iris_gen_handle_t* p) const { p->vtable->destroy(p); }
    };
    return [owned = std::unique_ptr<iris_gen_handle_t, Destroy>(h)]() mutable -> IrisResult {
        const auto* c = owned->vtable->next(owned.get());
        if (!c) return iris_end();
        iris::IrisValue v;
        v.type_id = c->type_id;
        v.payload = iris::IrisBuffer::from(c->payload, c->payload_size);
        return iris_val(std::move(v));
    };
}

// ── PluginBackendAdapter ──────────────────────────────────────────────────────
//
// Wraps an iris_irsh_backend_t* in the C++ IrshBackend interface.
// Owns the plugin handle; calls vtable->destroy() on destruction.
// Also owns the dlopen handle; calls dlclose() on destruction.

class PluginBackendAdapter final : public IrshBackend {
    iris_irsh_backend_t* plugin_;
    void*                dl_handle_;
    std::string          name_;

public:
    PluginBackendAdapter(iris_irsh_backend_t* p, void* dl, std::string name)
        : plugin_(p), dl_handle_(dl), name_(std::move(name)) {}

    ~PluginBackendAdapter() override {
        if (plugin_) {
            plugin_->vtable->destroy(plugin_);
            plugin_ = nullptr;
        }
        if (dl_handle_) {
            dlclose(dl_handle_);
            dl_handle_ = nullptr;
        }
    }

    std::string_view name() const override { return name_; }

    IrType check(std::string_view           op,
                 const BackendConfig&        config,
                 const IrType&              input,
                 const iris::TypeRegistry&  /*global*/,
                 std::vector<TypeError>&    errs,
                 Loc                        loc) const override
    {
        std::vector<const char*> field_ptrs;
        iris_backend_config_c_t  cfg_c = to_c_config(config, field_ptrs);
        iris_irtype_t            in_c  = to_c_irtype(input);

        struct ErrCtx { std::vector<TypeError>* errs; Loc loc; };
        ErrCtx ctx{&errs, loc};

        iris_irtype_t out = plugin_->vtable->check(
            plugin_, std::string{op}.c_str(), &cfg_c, in_c,
            [](void* raw_ctx, uint32_t line, uint32_t col, const char* msg) {
                auto* ec = static_cast<ErrCtx*>(raw_ctx);
                ec->errs->push_back(TypeError{{line, col}, msg});
            },
            &ctx);

        return from_c_irtype(out);
    }

    IrisGen make_gen(std::string_view          op,
                     const BackendConfig&       config,
                     const iris::TypeDescriptor* /*desc*/,
                     IrisGen                    upstream) override
    {
        std::vector<const char*> field_ptrs;
        iris_backend_config_c_t  cfg_c = to_c_config(config, field_ptrs);

        iris_gen_handle_t* up_h = upstream ? wrap_gen(std::move(upstream)) : nullptr;

        iris_gen_handle_t* out_h = plugin_->vtable->make_gen(
            plugin_, std::string{op}.c_str(), &cfg_c, up_h);

        return unwrap_gen(out_h);
    }
};

// ── FSM: UNLOADED → LOADED → VERIFIED → REGISTERED ───────────────────────────

enum class PluginState { Unloaded, Loaded, Verified, Registered };

static std::string try_load_one(const std::string& path, BackendRegistry& registry) {
    // LOADED: dlopen
    void* dl = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!dl)
        return path + ": dlopen failed: " + dlerror();

    // get factory symbol
    auto* factory = reinterpret_cast<iris_irsh_backend_factory_t>(
        ::dlsym(dl, IRIS_IRSH_BACKEND_EXPORT_SYMBOL));
    if (!factory) {
        ::dlclose(dl);
        return path + ": missing symbol '" IRIS_IRSH_BACKEND_EXPORT_SYMBOL "'";
    }

    iris_irsh_backend_t* plugin = factory();
    if (!plugin) {
        ::dlclose(dl);
        return path + ": factory returned null";
    }

    // ABI size check
    if (plugin->vtable->api_size != sizeof(iris_irsh_backend_vtable_t)) {
        plugin->vtable->destroy(plugin);
        ::dlclose(dl);
        return path + ": ABI mismatch (api_size mismatch)";
    }

    // VERIFIED: verify()
    if (plugin->vtable->verify(plugin) != 0) {
        plugin->vtable->destroy(plugin);
        ::dlclose(dl);
        return path + ": verify() failed";
    }

    const char* ns = plugin->vtable->name(plugin);
    if (!ns || *ns == '\0') {
        plugin->vtable->destroy(plugin);
        ::dlclose(dl);
        return path + ": name() returned empty string";
    }

    // REGISTERED
    try {
        registry.register_backend(
            std::make_unique<PluginBackendAdapter>(plugin, dl, std::string{ns}));
    } catch (const std::exception& e) {
        plugin->vtable->destroy(plugin);
        ::dlclose(dl);
        return path + ": register failed: " + e.what();
    }

    return {};  // success
}

static std::string default_plugin_dir() {
    const char* home = std::getenv("HOME");
    if (!home || *home == '\0') return {};
    return std::string{home} + "/.iris/plugins";
}

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────────

std::vector<std::string> load_plugins(BackendRegistry& registry,
                                      const std::string& plugin_dir) {
    std::string dir = plugin_dir.empty() ? default_plugin_dir() : plugin_dir;
    if (dir.empty()) return {};

    std::vector<std::string> errors;

    std::error_code ec;
    for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto& p = entry.path();
        if (p.extension() != ".so") continue;
        auto err = try_load_one(p.string(), registry);
        if (!err.empty()) errors.push_back(std::move(err));
    }

    return errors;
}

} // namespace iris::irsh
