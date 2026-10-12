/// \file
/// Tests for simulation/latency_config.hpp: the delay of adverse flow, which accepts any
/// DelaySpec alternative and defaults to no delay at all.

#include <variant>

#include <catch2/catch_test_macros.hpp>

#include <simulation/delay_spec.hpp>
#include <simulation/latency_config.hpp>

namespace sim = mms::simulation;

TEST_CASE("LatencyConfig holds the adverse-flow delay exactly", "[simulation][latency_config]") {
    const sim::LatencyConfig config{.adverse_flow_delay = sim::ExponentialDelaySpec{.mean = 0.01}};

    REQUIRE(std::holds_alternative<sim::ExponentialDelaySpec>(config.adverse_flow_delay));
    CHECK(std::get<sim::ExponentialDelaySpec>(config.adverse_flow_delay).mean == 0.01);
}

TEST_CASE("The adverse-flow delay defaults to none at all", "[simulation][latency_config]") {
    const sim::LatencyConfig config{};

    REQUIRE(std::holds_alternative<sim::ConstantDelaySpec>(config.adverse_flow_delay));
    CHECK(std::get<sim::ConstantDelaySpec>(config.adverse_flow_delay).delay == 0.0);
}

TEST_CASE("The adverse-flow delay accepts every DelaySpec alternative",
          "[simulation][latency_config]") {
    sim::LatencyConfig config{};

    for (const sim::DelaySpec& spec :
         {sim::DelaySpec{sim::ConstantDelaySpec{.delay = 0.5}},
          sim::DelaySpec{sim::UniformDelaySpec{.min = 0.0, .max = 1.0}},
          sim::DelaySpec{sim::ExponentialDelaySpec{.mean = 0.2}}}) {
        config.adverse_flow_delay = spec;
        CHECK(config.adverse_flow_delay.index() == spec.index());
    }
}
