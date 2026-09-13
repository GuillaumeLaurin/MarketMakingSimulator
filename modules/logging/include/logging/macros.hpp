#ifndef MMS_LOGGING_MACROS_HPP
#define MMS_LOGGING_MACROS_HPP

/// \file
/// LOG_TRACE/DEBUG/INFO/WARN/ERROR: thin macros wrapping Logger::log. Two
/// gating layers:
///   - compile-time: MMS_LOG_COMPILE_MIN_LEVEL (set by CMake per build type,
///     Info in release / Trace in dev; defaults to Trace here if the build
///     never defines it, so nothing is silently elided by accident). A level
///     below that floor expands to a no-op and vanishes from the binary
///     entirely - not even a branch.
///   - runtime: Logger::enabled(), checked inside MMS_LOG_IMPL BEFORE the
///     message is formatted, so a disabled call never pays for fmt::format
///     or for evaluating whatever expensive arguments it was given -
///     a plain function call could never do that, only a macro can.
/// fmt::format happens here, in the macro; Logger/Sink only ever see a
/// finished std::string_view.

#include <fmt/core.h>
#include <logging/level.hpp>
#include <logging/logger.hpp>

#ifndef MMS_LOG_COMPILE_MIN_LEVEL
#define MMS_LOG_COMPILE_MIN_LEVEL 0 // trace
#endif

#define MMS_LOG_IMPL(level, ...)                                                                   \
    do {                                                                                           \
        if (::mms::logging::Logger* mms_logger_ = ::mms::logging::current();                       \
            mms_logger_ != nullptr && mms_logger_->enabled(level)) {                               \
            mms_logger_->log(level, ::fmt::format(__VA_ARGS__));                                   \
        }                                                                                          \
    } while (0)

#if MMS_LOG_COMPILE_MIN_LEVEL <= 0
#define LOG_TRACE(...) MMS_LOG_IMPL(::mms::logging::Level::Trace, __VA_ARGS__)
#else
#define LOG_TRACE(...)                                                                             \
    do {                                                                                           \
    } while (0)
#endif

#if MMS_LOG_COMPILE_MIN_LEVEL <= 1
#define LOG_DEBUG(...) MMS_LOG_IMPL(::mms::logging::Level::Debug, __VA_ARGS__)
#else
#define LOG_DEBUG(...)                                                                             \
    do {                                                                                           \
    } while (0)
#endif

#if MMS_LOG_COMPILE_MIN_LEVEL <= 2
#define LOG_INFO(...) MMS_LOG_IMPL(::mms::logging::Level::Info, __VA_ARGS__)
#else
#define LOG_INFO(...)                                                                              \
    do {                                                                                           \
    } while (0)
#endif

#if MMS_LOG_COMPILE_MIN_LEVEL <= 3
#define LOG_WARN(...) MMS_LOG_IMPL(::mms::logging::Level::Warn, __VA_ARGS__)
#else
#define LOG_WARN(...)                                                                              \
    do {                                                                                           \
    } while (0)
#endif

#if MMS_LOG_COMPILE_MIN_LEVEL <= 4
#define LOG_ERROR(...) MMS_LOG_IMPL(::mms::logging::Level::Error, __VA_ARGS__)
#else
#define LOG_ERROR(...)                                                                             \
    do {                                                                                           \
    } while (0)
#endif

#endif // MMS_LOGGING_MACROS_HPP