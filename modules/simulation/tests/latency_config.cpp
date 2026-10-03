/// \file
/// Tests for simulation/latency_config.hpp: a plain aggregate of two
/// independent DelaySpecs, which must stay independent (setting one must
/// never affect the other) and must each accept any DelaySpec alternative.

#include <variant>

#include <catch2/catch_test_macros.hpp>

#include <simulation/delay_spec.hpp>
#include <simulation/latency_config.hpp>

namespace sim = mms::simulation;

TEST_CASE("LatencyConfig holds each delay exactly", "[simulation][latency_config]") {
    const sim::LatencyConfig config{.quote_delay = sim::ExponentialDelaySpec{.mean = 0.01},
                                    .adverse_flow_delay = sim::ConstantDelaySpec{.delay = 0.0}};

    REQUIRE(std::holds_alternative<sim::ExponentialDelaySpec>(config.quote_delay));
    CHECK(std::get<sim::ExponentialDelaySpec>(config.quote_delay).mean == 0.01);

    REQUIRE(std::holds_alternative<sim::ConstantDelaySpec>(config.adverse_flow_delay));
    CHECK(std::get<sim::ConstantDelaySpec>(config.adverse_flow_delay).delay == 0.0);
}

TEST_CASE("quote_delay and adverse_flow_delay are independent of each other",
          "[simulation][latency_config]") {
    sim::LatencyConfig config{.quote_delay = sim::ConstantDelaySpec{.delay = 1.0},
                              .adverse_flow_delay = sim::ConstantDelaySpec{.delay = 1.0}};

    config.quote_delay = sim::UniformDelaySpec{.min = 0.0, .max = 2.0};

    // Changing quote_delay must not have touched adverse_flow_delay.
    REQUIRE(std::holds_alternative<sim::ConstantDelaySpec>(config.adverse_flow_delay));
    CHECK(std::get<sim::ConstantDelaySpec>(config.adverse_flow_delay).delay == 1.0);
    REQUIRE(std::holds_alternative<sim::UniformDelaySpec>(config.quote_delay));
}

TEST_CASE("Each field independently accepts every DelaySpec alternative",
          "[simulation][latency_config]") {
    sim::LatencyConfig config{.quote_delay = sim::ConstantDelaySpec{.delay = 0.0},
                              .adverse_flow_delay = sim::ConstantDelaySpec{.delay = 0.0}};

    for (const sim::DelaySpec& spec :
         {sim::DelaySpec{sim::ConstantDelaySpec{.delay = 0.5}},
          sim::DelaySpec{sim::UniformDelaySpec{.min = 0.0, .max = 1.0}},
          sim::DelaySpec{sim::ExponentialDelaySpec{.mean = 0.2}}}) {
        config.quote_delay = spec;
        config.adverse_flow_delay = spec;
        CHECK(config.quote_delay.index() == spec.index());
        CHECK(config.adverse_flow_delay.index() == spec.index());
    }
}
