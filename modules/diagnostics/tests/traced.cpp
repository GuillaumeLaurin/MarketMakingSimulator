/// \file
/// Tests for Traced<Base> (<diagnostics/traced.hpp>). The central property
/// under test: Traced<std::invalid_argument> IS-A std::invalid_argument, so
/// every existing catch(const std::invalid_argument&) and
/// CHECK_THROWS_AS(..., std::invalid_argument) keeps working unmodified.

#include <stdexcept>
#include <string>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <diagnostics/traced.hpp>

using mms::diagnostics::InvalidArgument;
using mms::diagnostics::OutOfRange;
using mms::diagnostics::RuntimeError;
using mms::diagnostics::TracedBase;

static_assert(std::is_base_of_v<std::invalid_argument, InvalidArgument>);
static_assert(std::is_base_of_v<TracedBase, InvalidArgument>);
static_assert(std::is_base_of_v<std::runtime_error, RuntimeError>);
static_assert(std::is_base_of_v<TracedBase, RuntimeError>);
static_assert(std::is_base_of_v<std::out_of_range, OutOfRange>);
static_assert(std::is_base_of_v<TracedBase, OutOfRange>);

TEST_CASE("Traced<std::invalid_argument> is caught as std::invalid_argument, unmodified",
          "[diagnostics][traced]") {
    CHECK_THROWS_AS(throw InvalidArgument("bad value"), std::invalid_argument);
}

TEST_CASE("Traced<std::invalid_argument> preserves what() exactly", "[diagnostics][traced]") {
    try {
        throw InvalidArgument("bad value");
    } catch (const std::invalid_argument& e) {
        CHECK(std::string(e.what()) == "bad value");
    }
}

TEST_CASE("a caught std::exception& can be dynamic_cast to TracedBase for the rich dump",
          "[diagnostics][traced]") {
    try {
        throw InvalidArgument("bad value");
    } catch (const std::exception& e) {
        const auto* traced = dynamic_cast<const TracedBase*>(&e);
        REQUIRE(traced != nullptr);

        const std::string dump = traced->dump();
        CHECK_THAT(dump, Catch::Matchers::ContainsSubstring("bad value"));
        CHECK_THAT(dump, Catch::Matchers::ContainsSubstring("invalid_argument"));
    }
}

TEST_CASE("Traced<std::runtime_error> and Traced<std::out_of_range> behave the same way",
          "[diagnostics][traced]") {
    CHECK_THROWS_AS(throw RuntimeError("runtime failure"), std::runtime_error);
    CHECK_THROWS_AS(throw OutOfRange("index out of range"), std::out_of_range);

    try {
        throw RuntimeError("runtime failure");
    } catch (const std::exception& e) {
        const auto* traced = dynamic_cast<const TracedBase*>(&e);
        REQUIRE(traced != nullptr);
        CHECK_THAT(traced->dump(), Catch::Matchers::ContainsSubstring("runtime failure"));
    }
}

TEST_CASE("a plain std::exception (not Traced) is not a TracedBase", "[diagnostics][traced]") {
    try {
        throw std::invalid_argument("plain, not traced");
    } catch (const std::exception& e) {
        CHECK(dynamic_cast<const TracedBase*>(&e) == nullptr);
    }
}
