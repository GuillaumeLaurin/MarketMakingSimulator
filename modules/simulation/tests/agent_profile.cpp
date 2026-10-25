/// \file
/// Tests for simulation/agent_profile.hpp and simulation/account_rules.hpp. Both are
/// plain data: they keep what they are given, and the defaults are the prudent ones
/// (an agent may not sell short and has no position limit; collateral is full and
/// market orders carry a 5 % buffer).

#include <optional>
#include <string>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include <simulation/account_rules.hpp>
#include <simulation/agent_profile.hpp>

namespace sim = mms::simulation;

static_assert(std::is_aggregate_v<sim::AgentProfile>);
static_assert(std::is_aggregate_v<sim::AccountRules>);

TEST_CASE("An agent profile keeps what it is given", "[simulation][agent_profile]") {
    const sim::AgentProfile profile{
        .name = "maker", .initial_cash = 5e5, .can_short = true, .max_abs_position = 250};

    CHECK(profile.name == "maker");
    CHECK(profile.initial_cash == 5e5);
    CHECK(profile.can_short);
    REQUIRE(profile.max_abs_position.has_value());
    CHECK(*profile.max_abs_position == 250);
}

TEST_CASE("By default an agent may not sell short and has no position limit",
          "[simulation][agent_profile]") {
    const sim::AgentProfile profile{};

    CHECK_FALSE(profile.can_short);
    CHECK_FALSE(profile.max_abs_position.has_value());
}

TEST_CASE("The account rules default to full collateral and a 5 % market buffer",
          "[simulation][account_rules]") {
    constexpr sim::AccountRules rules;

    CHECK(rules.short_collateral_rate == 1.0);
    CHECK(rules.market_order_buffer == 0.05);
}

TEST_CASE("The account rules keep what they are given", "[simulation][account_rules]") {
    constexpr sim::AccountRules rules{.short_collateral_rate = 0.5, .market_order_buffer = 0.1};

    CHECK(rules.short_collateral_rate == 0.5);
    CHECK(rules.market_order_buffer == 0.1);
}
