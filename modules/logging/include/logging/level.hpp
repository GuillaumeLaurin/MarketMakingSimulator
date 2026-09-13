#ifndef MMS_LOGGING_LEVEL_HPP
#define MMS_LOGGING_LEVEL_HPP

/// \file
/// Log severity levels.

#include <cstdint>

namespace mms::logging {

enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error };

[[nodiscard]] constexpr const char* to_string(Level level) noexcept {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
    }
    return "?";
}

}  // namespace mms::logging

#endif // MMS_LOGGING_LEVEL_HPP