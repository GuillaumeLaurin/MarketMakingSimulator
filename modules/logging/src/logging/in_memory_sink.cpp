/// \file
/// Implementation of InMemorySink (<logging/in_memory_sink.hpp>).

#include <logging/in_memory_sink.hpp>

namespace mms::logging {

void InMemorySink::write(Level level, std::string_view message) {
    messages_.emplace_back(level, std::string(message));
}

const std::vector<std::pair<Level, std::string>>& InMemorySink::messages() const noexcept {
    return messages_;
}

} // namespace mms::logging