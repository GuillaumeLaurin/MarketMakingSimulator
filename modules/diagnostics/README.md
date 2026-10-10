# diagnostics

Exceptions that carry a stack trace, the thread and process ids and the version, captured
at the place they are thrown. `Traced<Base>` wraps a standard exception type without
changing it, so existing `catch` blocks keep working.

Depends on cpptrace and fmt. It does not depend on `logging`, and nothing else in the
project uses it yet.

Start with `diagnostics/diagnostics.hpp` (the umbrella), then `diagnostics/traced.hpp`.

Why it is built this way: [logging and diagnostics](../../docs/architecture/logging-and-diagnostics.md).
