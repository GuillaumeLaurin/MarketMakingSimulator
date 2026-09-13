/// \file
/// Tests for process_info(), capture_diagnostic_context() and format_dump()
/// (<diagnostics/diagnostic_context.hpp>).

#include <stdexcept>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <diagnostics/diagnostic_context.hpp>

using mms::diagnostics::capture_diagnostic_context;
using mms::diagnostics::DiagnosticContext;
using mms::diagnostics::format_dump;
using mms::diagnostics::process_info;
using mms::diagnostics::ProcessInfo;

TEST_CASE("process_info is stable across calls (computed once, cached)",
          "[diagnostics][diagnostic_context]") {
    const ProcessInfo& first = process_info();
    const ProcessInfo& second = process_info();

    CHECK(&first == &second); // same cached instance, not recomputed
    CHECK_FALSE(first.version.empty());
    CHECK_FALSE(first.build_type.empty());
}

TEST_CASE("process_info reports a build_type of Debug or Release",
          "[diagnostics][diagnostic_context]") {
    const ProcessInfo& info = process_info();
    CHECK((info.build_type == "Debug" || info.build_type == "Release"));
}

TEST_CASE("capture_diagnostic_context records the calling thread and a non-empty stacktrace",
          "[diagnostics][diagnostic_context]") {
    const DiagnosticContext context = capture_diagnostic_context("std::invalid_argument");

    CHECK(context.thread_id == std::this_thread::get_id());
    CHECK_FALSE(context.stacktrace.empty());
}

TEST_CASE("capture_diagnostic_context demangles the exception type name",
          "[diagnostics][diagnostic_context]") {
    const DiagnosticContext context =
        capture_diagnostic_context(typeid(std::invalid_argument).name());

    CHECK_THAT(context.exception_type, Catch::Matchers::ContainsSubstring("invalid_argument"));
}

TEST_CASE("format_dump assembles the message, type, process info and stacktrace",
          "[diagnostics][diagnostic_context]") {
    const DiagnosticContext context = capture_diagnostic_context("MyExceptionType");
    const std::string dump = format_dump("something went wrong", context);

    CHECK_THAT(dump, Catch::Matchers::ContainsSubstring("something went wrong"));
    CHECK_THAT(dump, Catch::Matchers::ContainsSubstring("MyExceptionType"));
    CHECK_THAT(dump, Catch::Matchers::ContainsSubstring(process_info().version));
    CHECK_THAT(dump, Catch::Matchers::ContainsSubstring(process_info().commit_hash));
    CHECK_THAT(dump, Catch::Matchers::ContainsSubstring(process_info().build_type));
    CHECK_THAT(dump, Catch::Matchers::ContainsSubstring("stacktrace"));
}
