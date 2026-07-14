# Irish — interpreter and terminal

Irish is the runtime for irsh scripts. It is a standalone binary that
interprets the irsh language, manages the TypeRegistry, drives backends,
and exposes the typed pipeline as a terminal experience.

Three goals in order of priority:

1. **Reliable** — type errors are caught at parse time, before any process runs.
   The pipeline never silently corrupts data. TypeId content-addressing means
   schema agreement is automatic, not a convention.

2. **Fast** — inline pipeline stages run in-process with no fork, no OS pipe,
   no serialization. `ls | filter | sort | print` is function calls in memory.
   External processes connect via zero-copy wire format.

3. **Clear** — tab completion is driven by TypeDescriptor field names.
   Error messages name the field and the type, not a memory address.
   `filter name > 1024` on a `Str` field fails immediately with an explanation.

---

## Three modes of operation

### 1. Interactive REPL

```
$ irish
>> ls "/var/log" | filter size > 4096 | sort by: size | print
```

- Stdin is a tty; readline with history and tab completion
- Stdout is a tty; `print` renders as human-readable text
- Session variables persist across prompts
- Tab completion suggests field names from the TypeDescriptor of the current stream
- Error output goes to stderr; pipeline state is unchanged on error

### 2. Script

```
#!/usr/bin/env irish
let logs = ls "/var/log" | filter size > 4096
logs | sort by: size | print
```

```bash
$ irish script.irsh
$ irish script.irsh --path /var/log --min-size 4096
```

- Arguments available as `$args.name` or `$args[0]`, `$args[1]`
- Stdout is text if it is a tty; wire format if it is a pipe (auto-detected)
- Exit code: 0 = success; 1 = runtime error; 2 = parse/type error; 3 = backend unavailable

### 3. Pipeline component

```bash
$ irish pipeline.irsh < typed_input > typed_output
$ some_iris_tool | irish filter.irsh | another_iris_tool
```

- Stdin is a pipe → irish reads wire-format IrisValue frames as `$stdin` stream
- Stdout is a pipe → print emits wire-format frames instead of text
- Makes irsh scripts composable with other Iris-aware processes without sockets

---

## Stdio behavior

Irish auto-detects the context via `isatty()` on startup:

```
stdin  is tty   → interactive REPL prompt
stdin  is pipe  → read wire-format IrisValue frames from $stdin
stdin  is file  → read irsh script (same as passing the file as argument)

stdout is tty   → print renders human-readable text
stdout is pipe  → print emits wire-format frames
```

Stderr is always text. irish never writes wire format to stderr.

---

## $stdin — typed stream from pipe

When stdin is a pipe, `$stdin` is available as a typed IrisValue stream:

```irsh
#!/usr/bin/env irish
# usage: ls /src | irish summary.irsh
$stdin | filter size > 1024 | map { name, size } | print
```

The first frame's `type_id` is resolved against the TypeRegistry to get the
TypeDescriptor. If the type is not registered, irish fails with a clear error
before processing any frames.

---

## Tab completion

Completion is driven entirely by the `TypeDescriptor` of the current stream.
There is no hardcoded list of field names — the list comes from whatever types
are registered at runtime.

```
>> ls | filter <TAB>
size    mtime   mode    type    name
>> ls | filter size <TAB>
>   <   >=  <=  ==  !=
>> ls | sort by: <TAB>
size    mtime   mode    type    name
```

If JavaBackend is connected and a class is loaded, its fields appear in
completion immediately after `register_class()` runs.

---

## Plugin and type discovery

### .so plugins

At startup irish scans `~/.iris/plugins/*.so` and the current directory
for `*.iris.so`. For each file it calls `dlopen`, looks for
`iris_backend_create`, and registers the backend by name.

```irsh
>> ls | @my_plugin("args") | print
```

### Java class discovery

```bash
$ irish --classpath ./analytics.jar
```

Irish calls `JavaBackend::register_class()` for every public class in the jar.
All discovered types are immediately available for tab completion and type
inspection.

```irsh
>> type com.example.LogEntry
struct com.example.LogEntry {
    timestamp : I64   offset=0   size=8
    level     : I32   offset=8   size=4
    message   : Str   offset=12  size=8
}
```

---

## Error handling

Every pipeline stage returns `expected<IrisValue, IrisError>`.
The default is to stop and print the error.

```
>> ls "/missing" | print
error [OsBackend]: /missing: no such file or directory
```

irsh operators override the default:

```irsh
let size = ls "/missing" | select size | head 1 ?? 0      # fallback value
ls "/missing" | print ?| echo "directory not found"       # fallback pipeline
```

Exit code 1 is set if any unhandled error reaches the top level of a script.

---

## Startup flags

```bash
irish                                # interactive REPL
irish script.irsh                    # run script
irish -e "ls | filter size > 1024 | print"   # inline expression
irish --classpath ./plugins.jar      # add jar to JavaBackend classpath
irish --ipc ./worker.sock            # set default @ipc socket
irish --types path/to/types.so       # load .so and register its types
```

---

## External process invocation

When irish encounters a bare path in a pipeline:

```irsh
ls | ./my_filter | print
```

It forks the binary and connects stdin/stdout via `pipe(2)`.
The binary must speak the Iris wire format (see IRSH.md, "Wire format" section).
`Ctrl-C` sends SIGINT to the child; SIGPIPE is caught and surfaced as
`IrisError::IpcDisconnected`.

Irish does not exec the binary through a shell — it calls `execve` directly,
which means no shell injection is possible from field values.

---

## Interoperability with regular Unix tools

Regular Unix tools do not speak wire format. Irish handles them explicitly:

```irsh
# text output from a regular command → irsh stream
lines grep foo /etc/passwd | filter text contains "bash" | print

# irsh stream → text for a regular command
ls | print | run wc -l
```

`lines cmd` forks the command via `execvp` (no shell), reads stdout line
by line, wraps each line as `TextLine { text: CStr[1024] }`.
`run cmd` is an alias. Neither uses `popen` or a shell — metacharacters
in arguments are not interpreted.

There is no silent fallback. If you pipe to a binary that does not speak wire
format, irish detects the framing error on the first frame and fails with a
clear message: "binary did not send a valid Iris frame header".

---

## DX goals for backend authors

Writing an Iris-aware utility should require nothing beyond:
1. Register the types you consume and produce (one call per type)
2. Read wire-format frames from stdin
3. Write wire-format frames to stdout
4. Exit 0 on clean EOF

No daemon, no socket path, no configuration file. The wire format is 12 bytes
of header then raw struct bytes — implementable in 50 lines in any language.
See IRSH.md, "Wire format" section, for the complete specification with
examples in Python and Rust.

---

## Future: IR and retranslation

Irish may eventually compile irsh scripts to an intermediate representation
(analogous to LLVM IR) that can be:

- **Interpreted** directly (current path)
- **Retranslated** to native code for hot pipeline stages
- **Serialised** and sent to a remote environment for execution

This would allow the same irsh script to run locally, on a remote machine,
or inside a WASM sandbox, with the IR as the portable unit. The TypeDescriptor
content-addressing already provides the type safety layer needed for this.

No timeline. Noted here so the interpreter design does not accidentally close
this door.
