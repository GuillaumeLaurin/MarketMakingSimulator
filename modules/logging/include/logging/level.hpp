#ifndef MMS_LOGGING_LEVEL_HPP
#define MMS_LOGGING_LEVEL_HPP

/// \file
/// Log severity levels.

#include <cstdint>

namespace mms::logging {

enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error };

}  // namespace mms::logging

#endif // MMS_LOGGING_LEVEL_HPP