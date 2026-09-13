#ifndef MMS_LOGGING_SINK_HPP
#define MMS_LOGGING_SINK_HPP

/// \file
/// Sink: where a formatted log message goes. A pure abstract interface.
/// Logger owns a collection of sinks and dispatches every message to each
/// of them.

#include <string_view>

#include <logging/level.hpp>

namespace mms::logging {

class Sink {
public:
    virtual ~Sink() = default;

    /// message is already formatted (fmt::format happens in the calling 
    /// macro, before this point) - Sink never sees format strings or
    /// variadic arguments, only the final text.
    virtual void write(Level level, std::string_view message) = 0;
};

} // namespace mms::logging

#endif // MMS_LOGGING_SINK_HPP