#ifndef MMS_LOGGING_LOGGER_HPP
#define MMS_LOGGING_LOGGER_HPP

/// \file
/// Logger: owns a runtime level floor and a set of sinks, dispatches an
/// already-formatted message to every sink when the level clears that floor.
/// One private Logger per simulation run - never a shared/global instance. 
/// set_current()/current() give call sites access to "their" run's
/// Logger via thread-local storage, without threading a Logger&
/// through every function signature in core.

#include <memory>
#include <string_view>
#include <vector>

#include <logging/level.hpp>
#include <logging/logging_export.hpp>
#include <logging/sink.hpp>

namespace mms::logging {

class MMS_LOGGING_EXPORT Logger {
public:
    /// min_level is the runtime floor: a level below it is dropped even if
    /// it survived compile-time gating (macros.hpp). Adjustable afterwards
    /// via set_min_level, but can never undo what the compile-time floor
    /// already elided entirely.
    explicit Logger(Level min_level = Level::Info) noexcept : min_level_(min_level) {}

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = default;
    Logger& operator=(Logger&&) = default;

    ~Logger() = default;

    void add_sink(std::unique_ptr<Sink> sink) { sinks_.push_back(std::move(sink)); }

    void set_min_level(Level level) noexcept { min_level_ = level; }

    [[nodiscard]] Level min_level() const noexcept { return min_level_; }

    [[nodiscard]] bool enabled(Level level) const noexcept { return level >= min_level_; }

    /// Dispatches an already-formatted message to every sink, provided
    /// enabled(level). message is NOT formatted here (no fmt involved) -
    /// that happens in the calling macro.
    void log(Level level, std::string_view message);

private:
    Level min_level_;
    std::vector<std::unique_ptr<Sink>> sinks_;
};

/// Sets the thread-local "current" Logger for this thread (typically once,
/// at the top of a simulation run). logger must outlive every subsequent
/// LOG_*(...) call on this thread, or be reset to nullptr first.
MMS_LOGGING_EXPORT void set_current(Logger* logger) noexcept;
/// The current thread's Logger, or nullptr if set_current() was never
/// called on this thread.
[[nodiscard]] MMS_LOGGING_EXPORT Logger* current() noexcept;

} // namespace mms::logging

#endif // MMS_LOGGING_LOGGER_HPP