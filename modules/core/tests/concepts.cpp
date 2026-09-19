/// \file
/// Tests for the OrderedArithmetic concept: what it accepts and, just as
/// important, the properties whose absence it must reject.

#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <core/concepts.hpp>

namespace {

/// Ordered and arithmetic, but with no default constructor.
struct NoDefault {
    explicit NoDefault(int v) : value(v) {}

    int value;

    auto operator<=>(const NoDefault&) const = default;

    NoDefault operator+(const NoDefault& other) const { return NoDefault{value + other.value}; }

    NoDefault operator-(const NoDefault& other) const { return NoDefault{value - other.value}; }
};

/// Default-constructible and arithmetic, but not ordered.
struct Unordered {
    int value = 0;

    Unordered operator+(const Unordered& other) const { return {value + other.value}; }

    Unordered operator-(const Unordered& other) const { return {value - other.value}; }
};

} // namespace

TEST_CASE("OrderedArithmetic accepts the built-in arithmetic types", "[core][concepts]") {
    STATIC_REQUIRE(mms::core::OrderedArithmetic<double>);
    STATIC_REQUIRE(mms::core::OrderedArithmetic<float>);
    STATIC_REQUIRE(mms::core::OrderedArithmetic<std::int64_t>);
}

TEST_CASE("OrderedArithmetic rejects a type with no default constructor", "[core][concepts]") {
    STATIC_REQUIRE_FALSE(mms::core::OrderedArithmetic<NoDefault>);
}

TEST_CASE("OrderedArithmetic rejects a type that is not totally ordered", "[core][concepts]") {
    STATIC_REQUIRE_FALSE(mms::core::OrderedArithmetic<Unordered>);
}

TEST_CASE("OrderedArithmetic rejects a type without arithmetic", "[core][concepts]") {
    STATIC_REQUIRE_FALSE(mms::core::OrderedArithmetic<std::string>); // ordered, default, but no `-`
}
