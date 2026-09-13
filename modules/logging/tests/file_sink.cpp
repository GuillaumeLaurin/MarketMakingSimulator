/// \file
/// Tests for FileSink against a REAL file on disk (created, written, read
/// back, and cleaned up), not an in-memory stand-in.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <logging/file_sink.hpp>

using mms::logging::FileSink;
using mms::logging::Level;

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

/// A path this test owns for its own scratch file, cleaned up on
/// destruction whether the test passes or throws.
class ScratchFile {
public:
    explicit ScratchFile(std::string name)
        : path_(std::filesystem::temp_directory_path() / std::move(name)) {
        std::filesystem::remove(path_); // in case a previous run left it behind
    }

    ScratchFile(const ScratchFile&) = delete;
    ScratchFile& operator=(const ScratchFile&) = delete;

    ~ScratchFile() { std::filesystem::remove(path_); }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("FileSink creates the file and writes prefixed, newline-terminated lines",
          "[logging][file_sink]") {
    ScratchFile scratch("mms_file_sink_basic.log");

    {
        FileSink sink(scratch.path());
        sink.write(Level::Info, "first line");
        sink.write(Level::Error, "second line");
    } // FileSink's ofstream flushes and closes here

    REQUIRE(std::filesystem::exists(scratch.path()));
    const std::string content = read_file(scratch.path());
    CHECK_THAT(content, Catch::Matchers::ContainsSubstring("[INFO] first line\n"));
    CHECK_THAT(content, Catch::Matchers::ContainsSubstring("[ERROR] second line\n"));

    // first line must actually come first
    CHECK(content.find("first line") < content.find("second line"));
}

TEST_CASE("FileSink truncates: a fresh FileSink on the same path discards old content",
          "[logging][file_sink]") {
    ScratchFile scratch("mms_file_sink_truncate.log");

    {
        FileSink sink(scratch.path());
        sink.write(Level::Info, "from the old run");
    }
    REQUIRE_THAT(read_file(scratch.path()), Catch::Matchers::ContainsSubstring("from the old run"));

    {
        FileSink sink(scratch.path());
        sink.write(Level::Info, "from the new run");
    }

    const std::string content = read_file(scratch.path());
    CHECK_THAT(content, Catch::Matchers::ContainsSubstring("from the new run"));
    CHECK_THAT(content, !Catch::Matchers::ContainsSubstring("from the old run"));
}

TEST_CASE("FileSink throws std::runtime_error when the file cannot be opened",
          "[logging][file_sink]") {
    // A path under a directory that does not exist can never be opened for
    // writing by std::ofstream (it does not create intermediate directories).
    const std::filesystem::path bad_path =
        std::filesystem::temp_directory_path() / "mms_file_sink_no_such_directory_xyz" / "file.log";

    CHECK_THROWS_AS(FileSink(bad_path), std::runtime_error);
}
