# Contract: TypeRegistry Freeze

## What freeze() does

`iris::TypeRegistry::global().freeze()` is called once in `main()` after all type registrations are complete (IRIS_TYPE macros run at static-init time; any runtime registrations via `iris_type_register` must happen before `freeze()`).

After `freeze()`:
- All assigned `TypeId` values are stable for the process lifetime.
- `find(name)` and `find(id)` return valid pointers; the pointed-to `TypeDescriptor` is immutable.
- `from_fields()` and `iris_type_register()` are blocked — they return error / throw.

## TypeId stability guarantee

Between `freeze()` and process exit, no TypeId will be reassigned, retired, or reused. Backends that cache a `TypeId` (e.g., `OsIrshBackend` caching `DirEntry`'s id at construction) are safe to do so after `freeze()`.

## Session types

`Session::session_types()` returns a **separate** `TypeRegistry` (not the global one). It is never frozen. The irsh interpreter uses it for `type Foo { ... }` declarations at runtime. Session types are not globally visible — they exist only within one Session.

## Order of freezes

```
static-init:   IRIS_TYPE macros execute → global TypeRegistry populated
main() step 5: iris::TypeRegistry::global().freeze()
main() step 6: g_registry.freeze()       (BackendRegistry)
```

TypeRegistry must be frozen BEFORE BackendRegistry, because backends may need to look up TypeIds in `check()` at parse time, and those TypeIds must be stable.

## What is NOT blocked after freeze()

- Reading the registry (`find`, iteration, field lookup).
- Session registry (`session_types()`) — it is a separate instance and remains mutable.
- Creating `IrisValue` instances — `type_id` is just a `uint64_t`; freeze doesn't affect values already created.

## Idempotency

`freeze()` is idempotent. Calling it multiple times is a no-op after the first call. Tests that call `freeze()` directly will not interfere with a subsequent `freeze()` in the same process.
