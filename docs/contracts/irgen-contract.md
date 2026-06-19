# Contract: IrisGen

## Definition

```cpp
using IrisGen = std::function<std::optional<iris::IrisValue>()>;
```

A lazy pull generator. Each call to the function object produces one value or signals end-of-stream.

---

## Return values

| Return value | Meaning |
|-------------|---------|
| `std::optional<IrisValue>` with a value | One pipeline element. |
| `std::nullopt` | End of stream. The generator is exhausted; further calls are undefined. |

Once a generator returns `std::nullopt`, it MUST NOT be called again. The executor respects this: it stops the drain loop on the first `nullopt`.

---

## Ownership

`IrisGen` is a `std::function` — it captures its state by value (or by shared_ptr for shared state). The captured state is destroyed when the `std::function` is destroyed (goes out of scope, is overwritten, or the holding struct is destroyed).

**IrisValue is move-only** (`OpaqueHandle` deletes copy constructor). When a generator returns a value, the caller must either move-consume it or not touch it after the next call.

```cpp
// Correct
while (auto v = gen()) {
    use(std::move(*v));  // or: use(*v) if use() takes by const-ref
}

// Wrong — v is invalidated by the next call
while (auto v = gen()) {
    process_later.push_back(*v);  // compile error: copy of IrisValue
}
// Fix: push_back(std::move(*v))
```

---

## Generator composition

A pipeline `src | f1 | f2` builds a chain:

```
gen0 = src.make_gen("ls", config, nullptr, {})   // source; upstream=null
gen1 = f1.make_gen("filter", pred, desc, gen0)   // gen1 captures gen0 by move
gen2 = f2.make_gen("sort", arg,  desc, gen1)     // gen2 captures gen1 by move
```

Each generator owns the previous one. Destroying gen2 destroys gen1 which destroys gen0.

---

## Sort — the only eager stage

`@base.sort` buffers all upstream values before returning the first sorted value:

```cpp
auto buf = make_shared<vector<IrisValue>>();
while (auto v = upstream()) buf->push_back(std::move(*v));
stable_sort(buf->begin(), buf->end(), cmp);
auto idx = make_shared<size_t>(0);
return [buf, idx]() -> optional<IrisValue> {
    if (*idx >= buf->size()) return nullopt;
    return std::move((*buf)[(*idx)++]);
};
```

`IrisValue` is moved out of the buffer one at a time. After the cursor passes a slot, that slot holds a moved-from value — do not revisit it.

---

## C plugin generator (iris_gen_handle_t)

The C ABI wraps `IrisGen` in an `iris_gen_handle_t`:

```c
typedef struct {
    const iris_irvalue_c_t* (*next)(iris_gen_handle_t* self); // NULL = end-of-stream
    void                    (*destroy)(iris_gen_handle_t* self);
} iris_gen_vtable_t;
```

### `next()` return value

The returned pointer is valid until the next call to `next()` or `destroy()`. The plugin reuses a single `iris_irvalue_c_t` slot. Callers must not hold the pointer across a `next()` call.

### `destroy()` — must be called exactly once

The plugin that creates a transform generator is responsible for calling `upstream->vtable->destroy(upstream)` in its own `destroy()`. The executor's `IrisGen` destructor (via shared_ptr) calls `destroy()` when the pipeline ends.

---

## Error propagation

There is no error channel in `IrisGen`. Runtime errors (permission denied, OOM, corrupt data) end the stream early: the generator returns `nullopt` after logging to stderr. Structured error propagation is deferred to a future version.
