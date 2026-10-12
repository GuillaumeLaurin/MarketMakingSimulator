/// \file
/// Tests for simulation/agent_spec.hpp and agent_timing.hpp: plain data, so these pin the
/// defaults and what an AgentSpec carries.

#include <variant>

#include <catch2/catch_test_macros.hpp>

#include <simulation/agent_spec.hpp>
#include <simulation/agent_timing.hpp>

namespace sim = mms::simulation;
namespace agents = mms::agents;

TEST_CASE("The default timing acts every unit of time, with no delay at all",
          "[simulation][agent_spec]") {
    const sim::AgentTiming timing{};
    CHECK(timing.decision_period == 1.0);
    REQUIRE(std::holds_alternative<sim::ConstantDelaySpec>(timing.order_delay));
    CHECK(std::get<sim::ConstantDelaySpec>(timing.order_delay).delay == 0.0);
    REQUIRE(std::holds_alternative<sim::ConstantDelaySpec>(timing.info_delay));
    CHECK(std::get<sim::ConstantDelaySpec>(timing.info_delay).delay == 0.0);
}

TEST_CASE("An AgentSpec carries a profile, a timing and a behavior as given",
          "[simulation][agent_spec]") {
    const sim::AgentSpec spec{
        .profile = {.name = "trend", .initial_cash = 500.0, .can_short = true},
        .timing = {.decision_period = 5.0, .order_delay = sim::ConstantDelaySpec{.delay = 0.2}},
        .behavior = agents::TrendFollowerSpec{
            .half_life = 10.0, .threshold = 0.01, .order_size = 3, .max_position = 20}};

    CHECK(spec.profile.name == "trend");
    CHECK(spec.profile.can_short);
    CHECK(spec.timing.decision_period == 5.0);
    CHECK(std::get<sim::ConstantDelaySpec>(spec.timing.order_delay).delay == 0.2);
    REQUIRE(std::holds_alternative<agents::TrendFollowerSpec>(spec.behavior));
    CHECK(std::get<agents::TrendFollowerSpec>(spec.behavior).max_position == 20);
}
