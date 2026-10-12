/// \file
/// Tests for agents/size_spec.hpp. At this stage DistributionSpec is pure
/// data (no sampler lives here yet, that is adverse_flow's job), so there is
/// not much behavior to test: this pins the variant's alternatives, field
/// names, and that it holds no hidden default that would silently paper over
/// a missing config value.

#include <variant>

#include <agents/size_spec.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/types.hpp>

namespace agents = mms::agents;
namespace mm = mms::mm;

TEST_CASE("UniformIntSpec holds its bounds exactly", "[agents][size_spec]") {
    constexpr agents::UniformIntSpec spec{.min = 1, .max = 50};

    CHECK(spec.min == 1);
    CHECK(spec.max == 50);
}

TEST_CASE("ExponentialSpec holds its mean exactly", "[agents][size_spec]") {
    constexpr agents::ExponentialSpec spec{.mean = 12.5};

    CHECK(spec.mean == 12.5);
}

TEST_CASE("DistributionSpec holds whichever alternative it was built from", "[agents][size_spec]") {
    const agents::DistributionSpec uniform = agents::UniformIntSpec{.min = 1, .max = 10};
    const agents::DistributionSpec exponential = agents::ExponentialSpec{.mean = 5.0};

    REQUIRE(std::holds_alternative<agents::UniformIntSpec>(uniform));
    CHECK(std::get<agents::UniformIntSpec>(uniform).max == 10);

    REQUIRE(std::holds_alternative<agents::ExponentialSpec>(exponential));
    CHECK(std::get<agents::ExponentialSpec>(exponential).mean == 5.0);
}

TEST_CASE("DistributionSpec has exactly the two documented alternatives", "[agents][size_spec]") {
    STATIC_REQUIRE(std::variant_size_v<agents::DistributionSpec> == 2);
}

TEST_CASE("UniformIntSpec and ExponentialSpec use mm::Quantity and double, not a narrower type",
          "[agents][size_spec]") {
    STATIC_REQUIRE(std::is_same_v<decltype(agents::UniformIntSpec::min), mm::Quantity>);
    STATIC_REQUIRE(std::is_same_v<decltype(agents::UniformIntSpec::max), mm::Quantity>);
    STATIC_REQUIRE(std::is_same_v<decltype(agents::ExponentialSpec::mean), double>);
}
