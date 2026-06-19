# Getting started

## 1. Enter the dev environment

```
nix develop
```

This brings in GCC 16, CMake, Ninja, replxx, GTest, and all other deps.
Run it once from the project root; the shell hook prints the active toolchain.

## 2. Build `irish`

```
cmake -B build -DIRIS_IRISH=ON -DIRIS_OS_BACKEND=ON -DIRIS_JAVA_BACKEND=OFF
cmake --build build --target irish
```

The binary lands at `build/irish`.

For a release build add `-DCMAKE_BUILD_TYPE=Release`.  
For AddressSanitizer add `-DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"`.

## 3. Run

```
./build/irish          # interactive REPL
./build/irish script.irsh   # run a script
./build/irish -e 'ls | head 3 | print'  # one-liner
```

Install to PATH for the session:

```
export PATH="$PWD/build:$PATH"
irish
```

---

## 4. Language basics

### Sources

```
ls               # stream of DirEntry  (name, size, kind)
ls "/var/log"    # with path
ps               # stream of ProcEntry (pid, name, cmdline, ...)
env              # stream of EnvEntry  (key, value)
./my-script      # fork+exec, stream of TextLine
```

### Pipelines

```
ls | filter size > 4096 | sort by: size desc | print
ps | filter name contains "fire" | select name | print
env | filter key starts_with "HOME" | print
```

`|` chains stages left to right. No subshell, no fork — stages run in-process.

### Collecting and session variables

```
let snap = ls | collect       # materialise into Vec<DirEntry> — ls runs once
snap | sort name | print      # replay from memory, ls not called again
snap | filter kind == "d"     # re-use the same snapshot
```

`let x = ls` without `collect` stores a lazy pipeline — re-runs on every use.

### Output

```
ls | print                          # print to stdout
ls | filter size > 0 | write "out.txt"   # write only when there is output (empty → no file)
```

### Type inspection

```
@base.types | print          # list all registered types
@base.type(DirEntry) | print # show fields of DirEntry
```

Or in the REPL:

```
:types           # show all types with field layout
:type DirEntry
```

### External programs

```
./git log --oneline | head 10 | print
./grep -r "TODO" src | print
```

`./path` is sugar for `@os.exec(path)` — fork+execvp, no shell, no injection.

---

## 5. REPL shortcuts

| Input | Action |
|-------|--------|
| Tab | complete stage name / field name / `@ns.op` |
| Up/Down | history navigation |
| line ending `\` | continuation — press Enter, keep typing |
| `:types` | inspect all registered types |
| `:type Name` | inspect one type |
| `:lex <expr>` | dump token stream (debug) |
| `exit` / Ctrl-D | quit |

Syntax highlighting and ZSH-style inline history hints are on by default.  
History is saved to `~/.irish_history`.

---

## 6. Running tests

```
cmake --build build --target iris_tests
ctest --test-dir build --output-on-failure
```

---

## 7. Project layout

```
src/irish/     — interpreter (lexer, parser, checker, executor, REPL)
src/backend/   — Iris core: IPC, channel, C ABI
src/os/        — OS backend: ls/ps/env streams, OsStream<> CRTP
sdk/           — C/C++/Python/Java headers for plugin authors
docs/          — architecture notes and this file
IRSH.md        — full language specification
IRISH.md       — interpreter design
ROADMAP.md     — what is done and what is next
```
