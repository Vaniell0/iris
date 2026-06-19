// SPDX-License-Identifier: MIT
/// @file   sdk/irsh_backend.h
/// @brief  Iris shell (irsh) plugin backend C ABI — dlopen-safe, zero C++ dependency.
///
/// A plugin .so must export:
///   iris_irsh_backend_t* iris_irsh_backend_create(void)
///
/// The loader calls create(), checks api_size, calls verify(), then name() to
/// determine the namespace, and registers the plugin with the BackendRegistry.
/// The registry calls vtable->destroy() on plugin unload / process exit.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── IrType (type-level, checker output) ──────────────────────────────────────

typedef enum {
    IRIS_IRTYPE_VOID      = 0,
    IRIS_IRTYPE_ANY       = 1,
    IRIS_IRTYPE_SCALAR    = 2,  ///< scalar_kind is valid
    IRIS_IRTYPE_STRUCT    = 3,  ///< elem_id holds TypeId
    IRIS_IRTYPE_STREAM    = 4,  ///< elem_id holds element TypeId
    IRIS_IRTYPE_VEC       = 5,  ///< elem_id holds element TypeId
    IRIS_IRTYPE_TEXT_LINE = 6,
    IRIS_IRTYPE_ALIAS     = 7,
} iris_irtype_kind_t;

/// Matches iris::PrimitiveKind in sdk/cpp/types.hpp
typedef enum {
    IRIS_PRIM_VOID  = 0,
    IRIS_PRIM_BOOL  = 1,
    IRIS_PRIM_I8    = 2,
    IRIS_PRIM_I16   = 3,
    IRIS_PRIM_I32   = 4,
    IRIS_PRIM_I64   = 5,
    IRIS_PRIM_F32   = 6,
    IRIS_PRIM_F64   = 7,
    IRIS_PRIM_STR   = 8,
    IRIS_PRIM_CSTR  = 9,
    IRIS_PRIM_BYTES = 10,
} iris_prim_kind_t;

typedef struct {
    uint8_t  kind;        ///< iris_irtype_kind_t
    uint8_t  scalar_kind; ///< iris_prim_kind_t (valid when kind == SCALAR)
    uint8_t  _pad[6];
    uint64_t elem_id;     ///< TypeId (valid when kind == STREAM / VEC / STRUCT)
} iris_irtype_t;

// ── BackendConfig (per-call arguments) ───────────────────────────────────────

typedef enum {
    IRIS_CONFIG_NONE       = 0, ///< monostate — no config (@base.print, @os.ps)
    IRIS_CONFIG_STRING     = 1, ///< raw string arg  (@base.write("file"), @os.ls("/"))
    IRIS_CONFIG_EXPR       = 2, ///< filter expression (opaque; use accessor helpers)
    IRIS_CONFIG_FIELD_LIST = 3, ///< list of field names (@base.map)
    IRIS_CONFIG_SORT_ARG   = 4, ///< field + direction (@base.sort)
} iris_config_kind_t;

typedef struct {
    const void* opaque; ///< internal Expr*; never NULL when kind == EXPR
} iris_expr_handle_t;

typedef struct {
    uint8_t kind;   ///< iris_config_kind_t
    uint8_t _pad[7];
    union {
        const char*            string;
        iris_expr_handle_t     expr;
        struct {
            const char* const* fields;
            size_t             count;
        } field_list;
        struct {
            const char* field; ///< field name; valid for plugin lifetime
            int         desc;  ///< non-zero = descending
        } sort_arg;
    };
} iris_backend_config_c_t;

// ── IrisValue (runtime, raw bytes) ───────────────────────────────────────────

typedef struct {
    uint64_t       type_id;
    const uint8_t* payload;      ///< raw bytes; valid until next call to next() or destroy()
    size_t         payload_size;
} iris_irvalue_c_t;

// ── Lazy pull generator ───────────────────────────────────────────────────────

typedef struct iris_gen_handle_s iris_gen_handle_t;

typedef struct {
    /// Returns pointer to the next value, or NULL at end-of-stream.
    /// The pointer is valid until the next call to next() or destroy().
    const iris_irvalue_c_t* (*next)(iris_gen_handle_t* self);
    /// Release all resources. Must be called exactly once.
    /// For transforms, the plugin is responsible for calling upstream->destroy().
    void (*destroy)(iris_gen_handle_t* self);
} iris_gen_vtable_t;

struct iris_gen_handle_s {
    const iris_gen_vtable_t* vtable;
    void*                    impl;
};

// ── IrshBackend plugin handle ─────────────────────────────────────────────────

typedef struct iris_irsh_backend_s iris_irsh_backend_t;

typedef struct {
    /// Must equal sizeof(iris_irsh_backend_vtable_t). Loader checks this for ABI compat.
    size_t api_size;

    /// Backend namespace (e.g., "myns"). Must remain valid for the plugin's lifetime.
    const char* (*name)(const iris_irsh_backend_t* self);

    /// Called once after dlopen. Return 0 if ready, non-zero to abort loading.
    int (*verify)(const iris_irsh_backend_t* self);

    /// Type-check op. For each error call emit_error(ctx, line, col, msg).
    /// Return the output IrType (often == input for filter/sort/head).
    iris_irtype_t (*check)(
        const iris_irsh_backend_t*     self,
        const char*                    op,
        const iris_backend_config_c_t* config,
        iris_irtype_t                  input,
        void (*emit_error)(void* ctx, uint32_t line, uint32_t col, const char* msg),
        void*                          error_ctx
    );

    /// Build a lazy pull generator.
    /// upstream is NULL for source operations (no prior pipeline stage).
    /// Caller owns the returned handle; must call vtable->destroy() when done.
    /// For transforms, the plugin must call upstream->vtable->destroy(upstream)
    /// when its own destroy() is invoked.
    iris_gen_handle_t* (*make_gen)(
        iris_irsh_backend_t*           self,
        const char*                    op,
        const iris_backend_config_c_t* config,
        iris_gen_handle_t*             upstream
    );

    /// Destroy this backend handle. Called once on plugin unload / process exit.
    void (*destroy)(iris_irsh_backend_t* self);

} iris_irsh_backend_vtable_t;

struct iris_irsh_backend_s {
    const iris_irsh_backend_vtable_t* vtable;
    void*                             impl;
};

/// Symbol that every plugin .so must export with C linkage.
#define IRIS_IRSH_BACKEND_EXPORT_SYMBOL "iris_irsh_backend_create"
/// Factory typedef for use with dlsym.
typedef iris_irsh_backend_t* (*iris_irsh_backend_factory_t)(void);

#ifdef __cplusplus
}
#endif
