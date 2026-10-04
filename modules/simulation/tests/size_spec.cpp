/// \file
/// Tests for simulation/size_spec.hpp. At this stage DistributionSpec is pure
/// data (no sampler lives here yet, that is adverse_flow's job), so there is
/// not much behavior to test: this pins the variant's alternatives, field
/// names, and that it holds no hidden default that would silently paper over
/// a missing config value.

#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <mm/types.hpp>

#include <simulation/size_spec.hpp>

namespace sim = mms::simulation;
namespace mm = mms::mm;

TEST_CASE("UniformIntSpec holds its bounds exactly", "[simulation][size_spec]") {
    constexpr sim::UniformIntSpec spec{.min = 1, .max = 50};

    CHECK(spec.min == 1);
    CHECK(spec.max == 50);
}

TEST_CASE("ExponentialSpec holds its mean exactly", "[simulation][size_spec]") {
    constexpr sim::ExponentialSpec spec{.mean = 12.5};

    CHECK(spec.mean == 12.5);
}

TEST_CASE("DistributionSpec holds whichever alternative it was built from",
          "[simulation][size_spec]") {
    const sim::DistributionSpec uniform = sim::UniformIntSpec{.min = 1, .max = 10};
    const sim::DistributionSpec exponential = sim::ExponentialSpec{.mean = 5.0};

    REQUIRE(std::holds_alternative<sim::UniformIntSpec>(uniform));
    CHECK(std::get<sim::UniformIntSpec>(uniform).max == 10);

    REQUIRE(std::holds_alternative<sim::ExponentialSpec>(exponential));
    CHECK(std::get<sim::ExponentialSpec>(exponential).mean == 5.0);
}

TEST_CASE("DistributionSpec has exactly the two documented alternatives",
          "[simulation][size_spec]") {
    STATIC_REQUIRE(std::variant_size_v<sim::DistributionSpec> == 2);
}

TEST_CASE("UniformIntSpec and ExponentialSpec use mm::Quantity and double, not a narrower type",
          "[simulation][size_spec]") {
    STATIC_REQUIRE(std::is_same_v<decltype(sim::UniformIntSpec::min), mm::Quantity>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::UniformIntSpec::max), mm::Quantity>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::ExponentialSpec::mean), double>);
}
