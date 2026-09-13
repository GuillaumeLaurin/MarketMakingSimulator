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
#include <logging/console_sink.hpp>

#if defined(_WIN32)
#include <io.h> // _dup, _dup2, _fileno, _close
#else
#include <unistd.h> // dup, dup2, close
#endif

using mms::logging::ConsoleSink;
using mms::logging::Level;

namespace {

struct Captured {
    std::string out;
    std::string err;
};

// Thin wrappers over the platform's fd-duplication primitives: MSVC and
// MinGW-w64 expose them with a leading underscore under <io.h>, POSIX
// exposes the unprefixed names under <unistd.h> (fileno lives in <cstdio>
// on both).
#if defined(_WIN32)
int duplicate_fd(int fd) {
    return _dup(fd);
}

int redirect_fd(int target_fd, int source_fd) {
    return _dup2(source_fd, target_fd);
}

int close_fd(int fd) {
    return _close(fd);
}

int fd_of(std::FILE* stream) {
    return _fileno(stream);
}
#else
int duplicate_fd(int fd) {
    return ::dup(fd);
}

int redirect_fd(int target_fd, int source_fd) {
    return ::dup2(source_fd, target_fd);
}

int close_fd(int fd) {
    return ::close(fd);
}

int fd_of(std::FILE* stream) {
    return ::fileno(stream);
}
#endif

/// Redirects both stdout and stderr to scratch files for the duration of
/// action, restores both, and returns everything each one received.
Captured capture_console(const std::function<void()>& action) {
    const auto out_path = std::filesystem::temp_directory_path() / "mms_console_sink_out.tmp";
    const auto err_path = std::filesystem::temp_directory_path() / "mms_console_sink_err.tmp";

    std::fflush(stdout);
    std::fflush(stderr);
    const int out_fd = fd_of(stdout);
    const int err_fd = fd_of(stderr);
    const int saved_out = duplicate_fd(out_fd);
    const int saved_err = duplicate_fd(err_fd);
    REQUIRE(saved_out != -1);
    REQUIRE(saved_err != -1);

    std::FILE* out_scratch = std::fopen(out_path.string().c_str(), "w");
    std::FILE* err_scratch = std::fopen(err_path.string().c_str(), "w");
    REQUIRE(out_scratch != nullptr);
    REQUIRE(err_scratch != nullptr);
    // _dup2 (Windows) returns 0 on success; POSIX dup2 returns the new fd
    // number instead (== out_fd/err_fd, never 0 for stdout/stderr) - only
    // -1 is a portable failure signal across both.
    REQUIRE(redirect_fd(out_fd, fd_of(out_scratch)) != -1);
    REQUIRE(redirect_fd(err_fd, fd_of(err_scratch)) != -1);

    action();

    std::fflush(stdout);
    std::fflush(stderr);
    std::fclose(out_scratch);
    std::fclose(err_scratch);
    redirect_fd(out_fd, saved_out);
    redirect_fd(err_fd, saved_err);
    close_fd(saved_out);
    close_fd(saved_err);

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
