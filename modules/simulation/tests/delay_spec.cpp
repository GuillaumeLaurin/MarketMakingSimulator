/// \file
/// Tests for simulation/delay_spec.hpp. Like size_spec, DelaySpec is pure
/// data at this stage (no sampler lives here, that is latency.cpp's job):
/// this pins the three alternatives, their field names and types, and that
/// DelaySpec does not accidentally share a name with size_spec's
/// DistributionSpec alternatives (the whole reason the two variants are
/// separate).

#include <type_traits>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include <simulation/delay_spec.hpp>
#include <simulation/size_spec.hpp>

namespace sim = mms::simulation;

TEST_CASE("ConstantDelaySpec holds its delay exactly, including zero", "[simulation][delay_spec]") {
    constexpr sim::ConstantDelaySpec zero{.delay = 0.0};
    constexpr sim::ConstantDelaySpec nonzero{.delay = 0.003};

    CHECK(zero.delay == 0.0);
    CHECK(nonzero.delay == 0.003);
}

TEST_CASE("UniformDelaySpec holds its bounds exactly", "[simulation][delay_spec]") {
    constexpr sim::UniformDelaySpec spec{.min = 0.001, .max = 0.05};

    CHECK(spec.min == 0.001);
    CHECK(spec.max == 0.05);
}

TEST_CASE("ExponentialDelaySpec holds its mean exactly", "[simulation][delay_spec]") {
    constexpr sim::ExponentialDelaySpec spec{.mean = 0.01};

    CHECK(spec.mean == 0.01);
}

TEST_CASE("DelaySpec holds whichever alternative it was built from", "[simulation][delay_spec]") {
    const sim::DelaySpec constant = sim::ConstantDelaySpec{.delay = 0.0};
    const sim::DelaySpec uniform = sim::UniformDelaySpec{.min = 0.0, .max = 1.0};
    const sim::DelaySpec exponential = sim::ExponentialDelaySpec{.mean = 0.2};

    REQUIRE(std::holds_alternative<sim::ConstantDelaySpec>(constant));
    REQUIRE(std::holds_alternative<sim::UniformDelaySpec>(uniform));
    REQUIRE(std::holds_alternative<sim::ExponentialDelaySpec>(exponential));
    CHECK(std::get<sim::ExponentialDelaySpec>(exponential).mean == 0.2);
}

TEST_CASE("DelaySpec has exactly the three documented alternatives, ConstantDelaySpec first",
          "[simulation][delay_spec]") {
    STATIC_REQUIRE(std::variant_size_v<sim::DelaySpec> == 3);
    STATIC_REQUIRE(
        std::is_same_v<std::variant_alternative_t<0, sim::DelaySpec>, sim::ConstantDelaySpec>);
}

TEST_CASE("Every DelaySpec field is a double, not mm::Quantity or anything narrower",
          "[simulation][delay_spec]") {
    STATIC_REQUIRE(std::is_same_v<decltype(sim::ConstantDelaySpec::delay), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::UniformDelaySpec::min), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::UniformDelaySpec::max), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::ExponentialDelaySpec::mean), double>);
}

TEST_CASE("DelaySpec and size_spec's DistributionSpec share no alternative type",
          "[simulation][delay_spec]") {
    // The whole reason the two variants are separate: if this ever stopped
    // compiling because a name collided, it would mean the two headers had
    // merged two unrelated concepts (a share count and a time) under one name.
    STATIC_REQUIRE_FALSE(std::is_same_v<sim::ConstantDelaySpec, sim::UniformIntSpec>);
    STATIC_REQUIRE_FALSE(std::is_same_v<sim::ExponentialDelaySpec, sim::ExponentialSpec>);
}
