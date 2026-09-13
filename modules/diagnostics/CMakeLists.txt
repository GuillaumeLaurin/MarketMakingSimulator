# MMS::diagnostics: rich error dumps (message, stacktrace, thread/process id,
# version + commit hash, build type) captured at the exact throw site via
# Traced<Base>, a thin wrapper that keeps the underlying std:: exception type
# intact (Traced<std::invalid_argument> IS a std::invalid_argument). Never
# depends on MMS::logging: it only ever produces a formatted string, the
# catch site decides how to report it.
mms_add_module(diagnostics
    PUBLIC_DEPENDS
        cpptrace::cpptrace
        fmt::fmt
        MMS::version
)
