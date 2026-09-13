/// \file
/// Implementation of FileSink (<logging/file_sink.hpp>).

#include <logging/file_sink.hpp>

#include <stdexcept>

#include <fmt/core.h>
#include <fmt/std.h>

namespace mms::logging {

FileSink::FileSink(const std::filesystem::path& path)
    : file_(path, std::ios::out | std::ios::trunc) {
    if (!file_.is_open()) {
        throw std::runtime_error(fmt::format("FileSink: could not open {} for writing", path));
    }
}

void FileSink::write(Level level, std::string_view message) {
    fmt::print(file_, "[{}] {}\n", to_string(level), message);
}

} // namespace mms::logging