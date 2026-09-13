#ifndef MMS_LOGGING_CONSOLE_SINK_HPP
#define MMS_LOGGING_CONSOLE_SINK_HPP

/// \file
/// ConsoleSink: writes to stdout (Trace/Debug/Info) or stderr (Warn/Error).
/// The one sink that's inherently shared across threads during a parallel
/// sensitivity sweep (one terminal), so writes are serialized by an internal
/// mutex - never a hot-path cost since console output only happens at
/// Info/Warn/Error frequency, never per-tick Trace/Debug.

#include <mutex>

#include <logging/logging_export.hpp>
#include <logging/sink.hpp>

namespace mms::logging {

class MMS_LOGGING_EXPORT ConsoleSink final : public Sink {
public:
    void write(Level level, std::string_view message) override;

private:
    std::mutex mutex_;
};

} // namespace mms::logging

#endif // MMS_LOGGING_CONSOLE_SINK_HPP