# Credits

## Origin of the typed shell

**Andrew ([@AuthorDriu][gh]) — the seed of the idea.**

The design conversation that became irsh started with Andrew. Over
months of shop-talk he came back, again and again, to one complaint:
the shell has no types. He had a long list of half-drafted ideas about
what a typed shell should look like — how commands should carry
schemas, how a pipeline should stop failing at the first mistyped
field, how declaring a struct once should be enough to teach every
verb what to do with it.

At some point I decided the ideas were too good to sit unbuilt, did a
week of research, and started **iris** as *iris for Java* — an
initial substrate meant to let Andrew's typed-shell design run on
top of the JVM. Everything else grew from there: the C ABI, the
non-JVM backends, the wire format, the interpreter.

Andrew also wanted to build the shell himself, as practice and to keep
his own version of the design. His result is **[JOBS][gh-jobs]**
(*Java OBject Script*) — a JVM-native scripting language with
first-class Java interop, deliberately simpler in the places where he
chose convergence-with-the-JVM over generality. Its pipeline moves
`String` and `ExecuteResult` values, its `exec(...)` captures
subprocess stdout/stderr/exit-code, its types are Java classes and
their aliases. Several of the ideas he described earliest but later
chose to drop or narrow for JOBS still live in irsh: the multi-runtime
substrate, IPC as a first-class stage between processes, FNV-64
content-addressed type identity.

Two implementations, one design lineage. Different specialisations,
different depths, same starting notebook.

[gh]:      https://github.com/AuthorDriu
[gh-jobs]: https://github.com/AuthorDriu/JOBS
