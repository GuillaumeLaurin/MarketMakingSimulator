#ifndef MMS_LOGGING_FILE_SINK_HPP
#define MMS_LOGGING_FILE_SINK_HPP

/// \file
/// FileSink: writes to a private log file, truncated on construction. Unlike
/// ConsoleSink, this is never shared across threads (one Logger, and so one
/// FileSink, per simulation run - see LOG6), so no internal synchronization
/// is needed here.

#include <filesystem>
#include <fstream>

#include <logging/logging_export.hpp>
#include <logging/sink.hpp>

namespace mms::logging {

class MMS_LOGGING_EXPORT FileSink final : public Sink {
public:
    explicit FileSink(const std::filesystem::path& path);

    void write(Level level, std::string_view message) override;

private:
    std::ofstream file_;
};

} // namespace mms::logging

#endif // MMS_LOGGING_FILE_SINK_HPP