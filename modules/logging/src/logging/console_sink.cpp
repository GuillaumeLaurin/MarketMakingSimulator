/// \file
/// Implementation of ConsoleSink (<logging/console_sink.hpp>).

#include <logging/console_sink.hpp>

#include <cstdio>

#include <fmt/core.h>

namespace mms::logging {

void ConsoleSink::write(Level level, std::string_view message) {
    std::FILE* stream = (level >= Level::Warn) ? stderr : stdout;

    const std::lock_guard<std::mutex> lock(mutex_);
    fmt::print(stream, "[{}] {}\n", to_string(level), message);
}

} // namespace mms::logging
