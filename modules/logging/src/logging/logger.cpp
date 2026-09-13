/// \file
/// Implementation of Logger, set_current() and current()
/// (<logging/logger.hpp>).

#include <logging/logger.hpp>

namespace mms::logging {

void Logger::log(Level level, std::string_view message) {
    if (!enabled(level)) {
        return;
    }
    for (const std::unique_ptr<Sink>& sink : sinks_) {
        sink->write(level, message);
    }
}

namespace {

thread_local Logger* current_logger = nullptr;

} // namespace

void set_current(Logger* logger) noexcept { current_logger = logger; }

Logger* current() noexcept { return current_logger;}

} // namespace mms::logging