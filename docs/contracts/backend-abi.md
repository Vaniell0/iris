# Contract: IrshBackend ABI

## Overview

Every irsh backend — built-in or plugin — implements one interface.
The C++ side is `iris::irsh::IrshBackend` (in `src/irish/backend/backend_registry.hpp`).
The C ABI for `.so` plugins is `iris_irsh_backend_vtable_t` (in `sdk/irsh_backend.h`).

---

## C++ interface

```cpp
class IrshBackend {
public:
    virtual std::string_view name() const = 0;

    virtual IrType check(std::string_view op,
                         const BackendConfig& config,
                         const IrType& input,
                         const TypeRegistry& global,
                         std::vector<TypeError>& errs,
                         Loc loc) const = 0;

    virtual IrisGen make_gen(std::string_view op,
                              const BackendConfig& config,
                              const TypeDescriptor* desc,
                              IrisGen upstream) = 0;

    virtual std::vector<std::string_view> ops() const { return {}; }
};
```

### `name()`

Returns the namespace string ("os", "base", "myns"). Must:
- Be a stable string (same value for the lifetime of the object).
- Not be empty.
- Not collide with any other registered backend (the registry rejects duplicates).

### `check(op, config, input, global, errs, loc)`

Called at parse time, once per `BackendCall` node in the AST.

| Parameter | Meaning |
|-----------|---------|
| `op` | Operation name ("ls", "filter", "sort", …). Never empty. |
| `config` | Parsed argument — `monostate`, `string`, `Expr`, `vector<string>`, or `SortArg`. |
| `input` | The IrType flowing into this call. `VoidType{}` for sources. |
| `global` | The (frozen) TypeRegistry — look up type descriptors here. |
| `errs` | Append `TypeError{loc, message}` for every violation found. |
| `loc` | Source location of this call — use it in TypeError entries. |

Returns the **output** IrType. For passthrough stages (filter, sort, head) return `input` unchanged.

Invariants:
- Must not modify `global`.
- Must not throw (append to `errs` instead).
- May be called concurrently from multiple threads (treat as `const`).

### `make_gen(op, config, desc, upstream)`

Called at execute time to build a lazy pull generator.

| Parameter | Meaning |
|-----------|---------|
| `op` | Same op string as check(). |
| `config` | Same config as check(). |
| `desc` | TypeDescriptor* for the stream's element type; null for text-line / void streams. |
| `upstream` | `IrisGen` for the preceding stage; null `std::function` for sources. |

Returns an `IrisGen = std::function<std::optional<iris::IrisValue>()>`.

Invariants:
- The returned generator may be called concurrently — but the executor always calls it from a single thread.
- `desc` pointer is valid for the duration of the pipeline execution.
- The generator owns `upstream` by move — do not call `upstream` after passing it.

### `ops()`

Returns the list of op names this backend handles. Used by the REPL for tab-completion hints. Optional — default returns empty.

---

## C ABI (sdk/irsh_backend.h)

The C ABI wraps the same semantics behind a vtable pointer:

```c
typedef struct {
    size_t api_size;   // must equal sizeof(iris_irsh_backend_vtable_t)
    const char* (*name)(const iris_irsh_backend_t*);
    int (*verify)(const iris_irsh_backend_t*);
    iris_irtype_t (*check)(...);
    iris_gen_handle_t* (*make_gen)(...);
    void (*destroy)(iris_irsh_backend_t*);
} iris_irsh_backend_vtable_t;
```

### `api_size` — ABI version guard

Set to `sizeof(iris_irsh_backend_vtable_t)` at compile time. The loader rejects any plugin whose `api_size != sizeof(iris_irsh_backend_vtable_t)` at the loader's compile time. When new optional slots are added to the vtable at the END, existing plugins survive loading (their `api_size` will be smaller; the loader checks before calling the new slot).

### `verify()` — pre-registration health check

Called once per plugin after `dlopen()`, before the plugin namespace appears in the registry. Return 0 for success, non-zero to abort loading (plugin is unloaded with a warning; remaining plugins continue).

### `check()` — C type mapping

C IrType (`iris_irtype_t`) maps to C++ `IrType` as follows:

| C kind constant | C++ type |
|----------------|---------|
| `IRIS_IRTYPE_VOID` | `VoidType{}` |
| `IRIS_IRTYPE_ANY` | `AnyType{}` |
| `IRIS_IRTYPE_SCALAR` | `ScalarType{kind}` |
| `IRIS_IRTYPE_STREAM` | `StreamType{elem_id}` |
| `IRIS_IRTYPE_VEC` | `VecType{elem_id}` |
| `IRIS_IRTYPE_TEXT_LINE` | `TextLineType{}` |

C BackendConfig (`iris_backend_config_c_t`) maps to C++ `BackendConfig`:

| C kind | C++ variant |
|--------|------------|
| `IRIS_CONFIG_NONE` | `monostate` |
| `IRIS_CONFIG_STRING` | `string` |
| `IRIS_CONFIG_EXPR` | `Expr` (opaque pointer; don't dereference from C) |
| `IRIS_CONFIG_FIELD_LIST` | `vector<string>` |
| `IRIS_CONFIG_SORT_ARG` | `SortArg` |

### `make_gen()` — generator ownership

The loader passes an `iris_gen_handle_t*` wrapping the upstream `IrisGen`. If the upstream is null (for sources), `make_gen()` receives `nullptr`. The plugin owns the upstream handle after receiving it and **must call `upstream->vtable->destroy(upstream)`** when its own generator is destroyed.

### `destroy()` — handle cleanup

Called once at plugin unload or process exit. Must release all resources including `impl` and the handle itself. After `destroy()` the plugin's `.so` may be `dlclose()`-d.

---

## Error handling

- `check()` errors: append to `errs` (C++) or call `emit_error` callback (C). Return `VoidType{}` on unrecoverable type mismatch.
- `make_gen()` errors: return an always-null generator `[]{ return nullopt; }`. Do not throw.
- Generator errors: silently end the stream (return `nullopt`). Structured error propagation is out of scope for irsh.
