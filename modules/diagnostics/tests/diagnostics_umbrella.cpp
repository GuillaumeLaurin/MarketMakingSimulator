/// \file
/// The <diagnostics/diagnostics.hpp> umbrella pulls in every public symbol
/// of the diagnostics layer.

#include <stdexcept>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <diagnostics/diagnostics.hpp>

TEST_CASE("<diagnostics/diagnostics.hpp> exposes the whole diagnostics layer",
          "[diagnostics][umbrella]") {
    using namespace mms::diagnostics;

    // diagnostic_context.hpp
    const ProcessInfo& info = process_info();
    CHECK_FALSE(info.version.empty());

    const DiagnosticContext context = capture_diagnostic_context("Whatever");
    CHECK_FALSE(context.stacktrace.empty());
    CHECK_FALSE(format_dump("msg", context).empty());

    // traced.hpp
    try {
        throw InvalidArgument("umbrella check");
    } catch (const std::exception& e) {
        const auto* traced = dynamic_cast<const TracedBase*>(&e);
        REQUIRE(traced != nullptr);
        CHECK_THAT(traced->dump(), Catch::Matchers::ContainsSubstring("umbrella check"));
    }
}
