#ifndef MMS_LOGGING_IN_MEMORY_SINK_HPP
#define MMS_LOGGING_IN_MEMORY_SINK_HPP

/// \file
/// InMemorySink: accumulates every message in a buffer instead of writing
/// anywhere.

#include <string>
#include <utility>
#include <vector>

#include <logging/logging_export.hpp>
#include <logging/sink.hpp>

namespace mms::logging {

class MMS_LOGGING_EXPORT InMemorySink final : public Sink {
public:
    void write(Level level, std::string_view message) override;

    [[nodiscard]] const std::vector<std::pair<Level, std::string>>& messages() const noexcept;

    /// Resets the buffer to empty.
    void clear() noexcept { messages_.clear(); }

private:
    std::vector<std::pair<Level, std::string>> messages_;
};

} // mms::logging

#endif // MMS_LOGGING_IN_MEMORY_SINK_HPP