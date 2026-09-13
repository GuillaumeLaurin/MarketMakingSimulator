/// \file
/// Tests for ConsoleSink against the REAL stdout/stderr streams (fd-level
/// redirection to scratch files, not a stub) - a Sink that only ever wrote to
/// an in-memory buffer in its own tests wouldn't actually prove it talks to
/// the console at all. Both streams are always redirected together so the
/// action under test never leaks output to the real console, whichever
/// stream it actually writes to.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <io.h> // _dup, _dup2, _fileno, _close (MSVC and MinGW-w64 both provide these)
#include <logging/console_sink.hpp>

using mms::logging::ConsoleSink;
using mms::logging::Level;

namespace {

struct Captured {
    std::string out;
    std::string err;
};

/// Redirects both stdout and stderr to scratch files for the duration of
/// action, restores both, and returns everything each one received.
Captured capture_console(const std::function<void()>& action) {
    const auto out_path = std::filesystem::temp_directory_path() / "mms_console_sink_out.tmp";
    const auto err_path = std::filesystem::temp_directory_path() / "mms_console_sink_err.tmp";

    std::fflush(stdout);
    std::fflush(stderr);
    const int out_fd = _fileno(stdout);
    const int err_fd = _fileno(stderr);
    const int saved_out = _dup(out_fd);
    const int saved_err = _dup(err_fd);
    REQUIRE(saved_out != -1);
    REQUIRE(saved_err != -1);

    std::FILE* out_scratch = std::fopen(out_path.string().c_str(), "w");
    std::FILE* err_scratch = std::fopen(err_path.string().c_str(), "w");
    REQUIRE(out_scratch != nullptr);
    REQUIRE(err_scratch != nullptr);
    REQUIRE(_dup2(_fileno(out_scratch), out_fd) == 0);
    REQUIRE(_dup2(_fileno(err_scratch), err_fd) == 0);

    action();

    std::fflush(stdout);
    std::fflush(stderr);
    std::fclose(out_scratch);
    std::fclose(err_scratch);
    _dup2(saved_out, out_fd);
    _dup2(saved_err, err_fd);
    _close(saved_out);
    _close(saved_err);

    Captured result;
    {
        std::ifstream in(out_path);
        std::ostringstream buf;
        buf << in.rdbuf();
        result.out = buf.str();
    }
    {
        std::ifstream in(err_path);
        std::ostringstream buf;
        buf << in.rdbuf();
        result.err = buf.str();
    }
    std::filesystem::remove(out_path);
    std::filesystem::remove(err_path);
    return result;
}

} // namespace

TEST_CASE("ConsoleSink writes Trace/Debug/Info to stdout, not stderr", "[logging][console_sink]") {
    ConsoleSink sink;

    const Captured c = capture_console([&] { sink.write(Level::Info, "hello from info"); });

    CHECK_THAT(c.out, Catch::Matchers::ContainsSubstring("[INFO] hello from info"));
    CHECK(c.err.empty());
}

TEST_CASE("ConsoleSink writes Warn/Error to stderr, not stdout", "[logging][console_sink]") {
    ConsoleSink sink;

    const Captured c = capture_console([&] { sink.write(Level::Error, "hello from error"); });

    CHECK_THAT(c.err, Catch::Matchers::ContainsSubstring("[ERROR] hello from error"));
    CHECK(c.out.empty());
}

TEST_CASE("ConsoleSink prefixes every level with its tag", "[logging][console_sink]") {
    ConsoleSink sink;

    const Captured trace = capture_console([&] { sink.write(Level::Trace, "a"); });
    CHECK_THAT(trace.out, Catch::Matchers::ContainsSubstring("[TRACE] a"));

    const Captured debug = capture_console([&] { sink.write(Level::Debug, "b"); });
    CHECK_THAT(debug.out, Catch::Matchers::ContainsSubstring("[DEBUG] b"));

    const Captured warn = capture_console([&] { sink.write(Level::Warn, "c"); });
    CHECK_THAT(warn.err, Catch::Matchers::ContainsSubstring("[WARN] c"));
}
