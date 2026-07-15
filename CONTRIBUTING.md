# Contributing to Iris

Iris is small on purpose. Most PRs are small on purpose. This document
tells you where to put a change, what a good change looks like, and how
licensing affects what you can do with the code.

---

## Where things live

| Repo                    | What it owns                                          |
|-------------------------|-------------------------------------------------------|
| **iris**                | Engine, language, interpreter — this repo             |
| **iris-backend-os**     | `@os.ls`, `@os.ps`, `@os.env`, `@os.exec`             |
| **iris-backend-base**   | `@base.filter`, `@base.sort`, `@base.map`, …          |
| iris-backend-math       | `@math.*` (community-owned, when there is one)        |
| iris-bridge-python      | ctypes / cffi wrapper over the C ABI                  |
| iris-bridge-rust        | safe Rust wrapper                                     |
| iris-bridge-go          | CGO wrapper                                           |

Inside this repo:

```
src/         core (C++), interpreter, built-in backends
sdk/         public C headers (MIT) — link target for out-of-tree code
include/     C++ public headers used by the core
tests/       GTest, one file per subsystem
docs/        reference, contracts, design notes
examples/    runnable irsh + host-language snippets
```

If your change is a new backend (`@foo.*`) it does **not** go in this
repo. See [`ECOSYSTEM.md`](ECOSYSTEM.md) for the process.

If your change is bookkeeping (name of a namespace, new SDK language),
it lives here.

---

## Building

```
nix develop            # GCC 16, CMake, Ninja, replxx, GTest
cmake -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

For a minimal core (no JVM, no OS backend):

```
cmake -B build -DIRIS_JAVA_BACKEND=OFF -DIRIS_OS_BACKEND=OFF
```

See `docs/getting-started.md` for `irish` usage.

---

## What a good change looks like

- **One thing at a time.** A PR that touches lexer + parser + docs is
  three PRs waiting to be split. A revert should never restore two
  broken features to fix one.
- **Test the behaviour, not the implementation.** GTest lives in
  `tests/`; one file per subsystem. Prefer end-to-end assertions
  (parse this, expect that output) over mocked internals.
- **No abstraction without a second caller.** If there is one user of
  a helper, inline it. Extract on the second occurrence, not the
  first. This is the same rule the standard library follows.
- **Comments explain WHY, not WHAT.** The code should say what.
  Comments earn their place when they encode a constraint that would
  surprise a reader six months later.
- **The lazier the API, the better.** `let x = ls "/big"` opens no
  file descriptors. If your change adds eager evaluation to a lazy
  stage, expect pushback.

We ship less, but we ship less.

---

## Coding style

- C++23 with named requirements (no `virtual` base classes for
  backends — see `concept Backend` in `include/backend.hpp`).
- No `using namespace` at file scope in headers.
- Every allocation has a clear owner. `unique_ptr` for uniques,
  `shared_ptr` only when sharing is documented.
- Names read left-to-right in the same order the code executes:
  `parse → check → lower → run`. If a helper reads them in reverse,
  it is a signal to reshape.
- `snake_case` for functions and members, `PascalCase` for types,
  `SCREAMING_SNAKE_CASE` for macros. Match what is around you.

Everything else is up to `clang-format` with the project config.

---

## Adding a new backend

Backends live in their own repos. From the user's perspective they
appear the moment the plugin `.so` is in `~/.iris/plugins/` or the
backend daemon is running.

Read in order:

1. [`ECOSYSTEM.md`](ECOSYSTEM.md) — the three modes (inline / IPC /
   process) and how to pick.
2. `docs/contracts/backend-abi.md` — what your backend must implement.
3. `docs/contracts/plugin-lifecycle.md` — how irish will load it.
4. `docs/contracts/irgen-contract.md` — how your generators must
   behave under composition.

The 30-line receiver examples in `docs/reference/wire-format.md` show
the minimum a peer needs to speak.

---

## Adding a new language SDK

The C ABI (`sdk/iris_registry.h`, `sdk/iris_backend.h`,
`sdk/irsh_backend.h`) is MIT and is the only public surface. Any
language with a C FFI can bind to it directly.

Existing bridges live in their own repos (`iris-bridge-python`,
`iris-bridge-rust`, etc.). Add a new one when there is a real
consumer, not to complete a matrix.

---

## Documentation

- Reference (`docs/reference/`) is versioned with the code — if you
  change behaviour, update the reference in the same PR.
- Design (`docs/design/`) is plans, not promises. Update it when the
  plan changes; do not gate PRs on design docs matching reality.
- Contracts (`docs/contracts/`) are covenants with plugin authors.
  Breaking one is a major-version event.

---

## Reviewing

Any maintainer can approve. A change that touches the wire format,
`TypeId`, or the C ABI needs two approvals — those are compatibility
promises.

The review question is not "is this correct?" but "would I want to
maintain this?". A working PR that adds ten new lines of surface for
a two-line problem is a rewrite request, not a merge.

---

## Licensing — read this before pasting code

Iris is a mixed-licence project:

| Part                                     | Licence     | What it means for you                                                        |
|------------------------------------------|-------------|------------------------------------------------------------------------------|
| Core: `src/`, `include/` (not `os.hpp`)  | GPL-2.0     | Derived works are GPL. Changes stay open.                                    |
| OS layer: `src/os/`, `include/os.hpp`    | Apache-2.0  | Can be linked without copyleft propagation.                                  |
| SDK: `sdk/*` (headers + Python)          | MIT         | Any project can build against the SDK without inheriting GPL.                |

There is a **build-dependency exception**: the core may be linked
against Apache-2.0 dependencies (specifically `stdexec`) enabled via
CMake options without triggering copyleft. See `LICENSE` for the
exact wording.

Practical consequence for a contributor:

- **Third-party code from a permissive project (MIT / BSD / Apache):**
  can be added to any part of the tree.
- **GPL-3.0-only code:** cannot be added to the core (GPL-2 vs GPL-3
  incompatibility) or the SDK (MIT can absorb GPL, but the SDK
  guarantees stay MIT). It can go in a separate GPL-3 downstream
  project that depends on Iris.
- **Apache-2.0 code:** can go in `src/os/` freely. In the core it
  needs to be a build-time dependency behind a CMake flag.

If you are unsure, open an issue before writing the code.

---

## Release cadence

Versions are cut when a meaningful set of ROADMAP tasks lands. There
is no timed schedule. `TypeId`, the wire format, and the C ABI are
under semver — breaking them is a major bump; additions are minor.

Interpreter UX (completion, colours, hints, `:type` output) is
unstable and can change between minor versions.

---

## Where to ask

- **Design question:** GitHub Discussions in the `iris` repo.
- **Bug you can reproduce:** GitHub Issues with a minimal `.irsh`
  script and the expected vs actual output.
- **Security issue:** email the maintainer address in the repo
  metadata. Do not open a public issue.

The maintainers do not have a Slack, a Discord, or an office. All
persistent conversation is in GitHub so the newcomer six months from
now can catch up by reading it.
