# Contract: Plugin Lifecycle

## FSM

```
UNLOADED
  ↓  scan ~/.iris/plugins/*.so
LOADED          dlopen() succeeded; iris_irsh_backend_create symbol found
  ↓  call factory() → iris_irsh_backend_t*
  ↓  check api_size == sizeof(iris_irsh_backend_vtable_t)
  ↓  call vtable->name() → namespace string
  ↓  conflict check: namespace not already in BackendRegistry
VERIFIED        vtable->verify() returned 0
  ↓  BackendRegistry::register_backend(PluginBackendAdapter{handle, dl})
REGISTERED      backend is live in the registry
  ↓  (all plugins processed)
SEALED          BackendRegistry::freeze() called; no further registrations
```

## Transitions

### UNLOADED → LOADED

`load_plugins()` scans `~/.iris/plugins/` (or the directory passed as argument).
For each `*.so` file:

1. `dlopen(path, RTLD_NOW | RTLD_LOCAL)` — `RTLD_NOW` resolves all symbols immediately so missing deps fail here rather than at call time. `RTLD_LOCAL` prevents symbol pollution between plugins.
2. `dlsym(handle, "iris_irsh_backend_create")` — missing symbol → FAILED (dlclose, warn, continue).
3. Call factory → `iris_irsh_backend_t*` — null return → FAILED.
4. `api_size` check — mismatch → FAILED (call `destroy()` first, then dlclose).
5. `name()` → conflict check against already-registered namespaces → FAILED if collision.

### LOADED → VERIFIED

`vtable->verify(plugin)` is called. Non-zero → FAILED (call `destroy()`, dlclose, warn, continue).

### VERIFIED → REGISTERED

`registry.register_backend(make_unique<PluginBackendAdapter>(plugin, dl_handle, name))`.

`PluginBackendAdapter` owns both the plugin handle (calls `destroy()` in its destructor) and the `dlhandle` (calls `dlclose()` in its destructor). This ensures cleanup happens in LIFO order on process exit.

### REGISTERED → SEALED

After all built-in backends and plugins are registered, `BackendRegistry::freeze()` is called in `main()`. From this point `register_backend()` throws `std::logic_error`. Any late-arriving code that tries to register a backend gets an exception; the exception is caught, logged as a warning, and the backend is not loaded.

## Failure handling

- Every failure is non-fatal: log a warning to stderr, continue with remaining plugins.
- Failed plugins are fully unloaded (dlclose) before continuing.
- On verification failure, `destroy()` is called before `dlclose()`.
- Namespace collision: the second plugin with the same namespace is rejected. The first-registered wins.

## Built-in backends

Built-in backends (`BaseIrshBackend`, `OsIrshBackend`) are registered before the plugin scan and never fail VERIFIED. They are not `dlopen`-managed — they are compiled into the `irish` binary.

## Startup sequence in main()

```cpp
g_registry.register_backend(make_unique<BaseIrshBackend>());   // @base
g_registry.register_backend(make_unique<OsIrshBackend>());     // @os
for (auto& err : load_plugins(g_registry))
    fprintf(stderr, "irish: plugin warning: %s\n", err.c_str());
g_registry.freeze();
iris::TypeRegistry::global().freeze();
// → REPL or script
```

## Plugin discovery directory

Default: `$HOME/.iris/plugins/`. Override by passing a path to `load_plugins()`.
Files must have `.so` extension to be considered.
Symlinks are followed (via `std::filesystem::is_regular_file()` which follows links).
Non-`.so` files and subdirectories are silently skipped.
