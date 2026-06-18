# irsh — syntax

A typed shell built on Iris. Every value in the pipeline is an `IrisValue`
with a known `TypeDescriptor`. No text parsing, no grep, no awk.

Type errors are caught at input time — before any process runs.

---

## Values

Struct literals map directly to registered types:

```
Point { x: 3, y: 4 }
DirEntry { size: 1024, name: "foo.cpp" }
```

Field shorthand when a session variable has the same name:

```
let x = 42
Point { x, y: 0 }    # → Point { x: 42, y: 0 }
```

**Implementation:** parser looks up the name in `TypeRegistry::global()`,
constructs `IrisValue` by writing field values into raw bytes at the offsets
from `TypeDescriptor`. Type error at input if field name unknown or kind
mismatch — `filter name > 1024` on a `Str` field is rejected before running.

---

## Session variables

```
let p  = Point { x: 5, y: 10 }
let logs = ls "/var/log"          # eager: vector<IrisValue> stored in session
```

Session is a `map<string, IrisValue>`. Streams are stored as snapshots.
Variables resolve before pipeline execution. `let` only assigns on success —
if the right-hand side errors, the variable is not created.

---

## Pipelines — sequential `|`

```
Point { x: 3, y: 4 } | @ipc
ps | filter state == "R" | @java("Monitor.analyze")
ls "/var/log" | filter size > 1024 | sort by: size | print
```

Each `|` is a `FnBackend` or named backend wired into a P2300 sender chain,
executed with `iris::sync_wait()`. On error the chain short-circuits.

**Implementation:** `iris::just(val) | iris::via(b0) | iris::via(b1) | ...`

---

## Pipelines — parallel `&`

```
ls / & ps & env | @java("Collector.merge")
```

All branches start simultaneously, results joined before the next stage.

**Implementation:** `stdexec::when_all(s0, s1, s2) | then(merge_fn)`

---

## Fire and forget `&!`

```
ps &!
```

Detaches onto the thread pool. No result, no error propagation.

**Implementation:** `iris::schedule_on(tp)` without `sync_wait`.

---

## Streams vs single values

`ls`, `ps`, `env` emit streams (`vector<IrisValue>`).
Struct literals and `head` emit a single value.

`filter`, `sort`, `map`, `select` are always element-wise — no `each` needed.

When a backend expects a single value and receives a stream, use `each`:

```
ps | filter state == "R" | each | @java("Monitor.analyze")
```

Without `each`, the stream is passed whole — useful when the Java method
accepts a list type.

---

## Backends

| Syntax | Backend | Notes |
|--------|---------|-------|
| `\| @ipc` | `IpcBackend` | default socket |
| `\| @ipc("./worker.sock")` | `IpcBackend` | named socket |
| `\| @java("Class.method")` | `JavaBackend` | JVM via `RuntimeManager` |
| `\| @os.ls("/path")` | `OsBackend::ls` | emits `DirEntry` stream |
| `\| @os.ps` | `OsBackend::ps` | emits `ProcessInfo` stream |
| `\| @os.env` | `OsBackend::env` | emits `EnvVar` stream |
| `\| print` | built-in | pretty-prints fields from `TypeDescriptor` |
| `\| filter <expr>` | built-in `FnBackend` | predicate over typed fields |
| `\| sort by: <field>` | built-in `FnBackend` | sorts stream by named field |
| `\| map { f1, f2 }` | built-in `FnBackend` | project fields into new type |
| `\| select <field>` | built-in `FnBackend` | extract one field as scalar |
| `\| head <n>` | built-in | take first n elements |

---

## Filter expressions

Predicates are resolved against the `TypeDescriptor` of the incoming stream.
Wrong field name or mismatched kind → error at input, not at runtime.

**Numeric** (I8 / I16 / I32 / I64 / F32 / F64):
```
filter size > 1024
filter pid == 1
filter load >= 0.8
```

**String** (Str):
```
filter name == "init"
filter name contains ".cpp"
filter name starts_with "lib"
filter name ends_with ".so"
filter name matches ".*\.so\.\d+"
```

**Bool:**
```
filter valid == true
```

---

## Projection — map and select

`map { f1, f2 }` creates a new anonymous type with only those fields:

```
ps | map { name, pid } | print
# prints: name: "init"  pid: 1
```

`select f` extracts a single field as a scalar `IrisValue`:

```
let first_pid = ps | filter name == "init" | select pid | head 1
```

**Implementation:** `map` registers a temporary `TypeDescriptor` on the fly
from the parent descriptor's matching fields. `select` wraps the raw field
bytes as a scalar with its `PrimitiveKind` as the type.

---

## Error handling

Every stage returns `expected<IrisValue, IrisError>`. Default: stop + print.

```
>> Sum { a: 10 } | @ipc("missing.sock")
error [IpcBackend]: connect failed: no such file or directory
```

**Fallback value `??`** — replace error with a default:

```
let pid = ps | filter name == "init" | select pid | head 1 ?? 0
```

**Fallback pipeline `?|`** — run alternative on error:

```
ps | filter name == "init" | select pid | head 1 ?| echo "not found"
```

`&!` swallows all errors by design.

Type errors (wrong field, kind mismatch) are caught at input — they never
reach the runtime. Runtime errors (`expected` failures) propagate through
the P2300 chain and surface at `sync_wait`.

---

## Type inspection

```
type Point          # inspect one type
types               # list all registered types
```

Output:

```
struct Point {
    x : I32   offset=0  size=4
    y : I32   offset=4  size=4
}
total: 8 bytes  id: 0x9e3f...
```

Zero JVM calls. Works before any backend is connected.

---

## Startup flags

```
irsh --classpath ./plugins.jar     # add jar to JavaBackend classpath
irsh --ipc ./worker.sock           # default @ipc socket
irsh --script foo.irsh             # run script, then exit
```

---

## Full session example

```
>> type DirEntry
struct DirEntry { size: I64, mtime: I64, mode: I32, type: I32, name: CStr[256] }

>> let logs = ls "/var/log" | filter size > 4096
>> logs | sort by: size | print

>> ps | filter state == "R" | map { name, pid } | print
name: "kworker"  pid: 42
name: "irsh"     pid: 91

>> let pid = ps | filter name == "init" | select pid | head 1 ?? 0
>> pid
1

>> ls "/src" & ps | @java("Dashboard.render")
Dashboard { files: 142, procs: 87 }

>> @ipc("missing.sock") ?| echo "worker offline"
worker offline
```
