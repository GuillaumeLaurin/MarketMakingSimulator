# MMS::logging: leveled logging (Trace/Debug/Info/Warn/Error), pluggable sinks
# (console, file, in-memory), one Logger per simulation run. fmt is exposed
# publicly (macros.hpp formats with fmt::format before dispatching to Logger).
mms_add_module(logging
    PUBLIC_DEPENDS
        fmt::fmt
)

# Compile-time floor for LOG_TRACE/DEBUG/INFO/WARN/ERROR (macros.hpp): a level
# below this value expands to a no-op and is elided from the binary entirely,
# not just skipped at runtime. PUBLIC: macros.hpp is a public header, so every
# consumer's translation unit must see the same value the library itself was
# built with, or it would silently fall back to the header's own default.
# Level values: Trace=0, Debug=1, Info=2, Warn=3, Error=4 (logging/level.hpp).
target_compile_definitions(mms_logging PUBLIC
    "MMS_LOG_COMPILE_MIN_LEVEL=$<IF:$<CONFIG:Release>,2,0>" # Info in Release, Trace otherwise
)
