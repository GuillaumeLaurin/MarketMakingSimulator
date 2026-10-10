# logging

Leveled logging (Trace, Debug, Info, Warn, Error) with pluggable sinks: the console, a
file, or an in-memory buffer for tests. One `Logger` per run, never a global.

Depends on fmt. Nothing else in the project uses it yet.

Start with `logging/logging.hpp` (the umbrella). Call sites use the macros in
`logging/macros.hpp`, which can be compiled out entirely below a level set by CMake.

Why it is built this way: [logging and diagnostics](../../docs/architecture/logging-and-diagnostics.md).
