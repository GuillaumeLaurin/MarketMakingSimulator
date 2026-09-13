/// \file
/// Implementation of FileSink (<logging/file_sink.hpp>).

#include <stdexcept>

#include <fmt/core.h>
#include <fmt/ostream.h> // fmt::print(std::ostream&, ...) - write() prints to file_
#include <logging/file_sink.hpp>

namespace mms::logging {

FileSink::FileSink(const std::filesystem::path& path)
    : file_(path, std::ios::out | std::ios::trunc) {
    if (!file_.is_open()) {
        // path.string(), not path itself: fmt's std::filesystem::path formatter
        // (<fmt/std.h>) routes through a consteval-checked FMT_STRING call that
        // clang-tidy's stricter Sema trips on for this fmt/Clang combination,
        // even though a plain compile accepts it.
        throw std::runtime_error(
            fmt::format("FileSink: could not open {} for writing", path.string()));
    }
}

void FileSink::write(Level level, std::string_view message) {
    fmt::print(file_, "[{}] {}\n", to_string(level), message);
}

} // namespace mms::logging