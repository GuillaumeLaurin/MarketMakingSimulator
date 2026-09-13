# MMS::logging: leveled logging (Trace/Debug/Info/Warn/Error), pluggable sinks
# (console, file, in-memory), one Logger per simulation run. fmt is exposed
# publicly (macros.hpp formats with fmt::format before dispatching to Logger).
mms_add_module(logging
    PUBLIC_DEPENDS
        fmt::fmt
)
