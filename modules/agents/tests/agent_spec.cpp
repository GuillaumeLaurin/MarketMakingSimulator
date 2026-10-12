/// \file
/// Tests for agents/agent_spec.hpp: make_agent builds the agent each kind of spec describes,
/// and passes on the refusal of a spec the agent does not accept.

#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <variant>

#include <agents/agent_spec.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/strategy_config.hpp>

#include <math/types.hpp>

namespace {

namespace agents = mms::agents;
namespace mm = mms::mm;
namespace math = mms::math;

agents::MarketMakerSpec market_maker(mm::Ticks threshold = 1) {
    return agents::MarketMakerSpec{
        .strategy = {.gamma = 2.0,
                     .k = math::Vector::Constant(1, 1.0),
                     .correlation = math::Matrix::Identity(1, 1),
                     .estimators = {mm::ConstantSpec{.sigma = 0.2}},
                     .horizon = mm::ConstantHorizon{.tau = 1.0},
                     .quote_sizes = {10},
                     .tick_sizes = {0.01},
                     .limits = {.max_abs_position = {1000}, .max_portfolio_risk = std::nullopt},
                     .min_samples = 0},
        .requote_threshold_ticks = threshold};
}

} // namespace

static_assert(std::variant_size_v<agents::AgentBehavior> == 3);
static_assert(
    std::is_same_v<std::variant_alternative_t<0, agents::AgentBehavior>, agents::MarketMakerSpec>);
static_assert(
    std::is_same_v<std::variant_alternative_t<1, agents::AgentBehavior>, agents::InformedFlowSpec>);
static_assert(
    std::is_same_v<std::variant_alternative_t<2, agents::AgentBehavior>, agents::NoiseTraderSpec>);

TEST_CASE("make_agent builds a market maker from its spec", "[agents][agent_spec]") {
    const agents::AgentBehavior behavior = market_maker();

    const std::unique_ptr<agents::Agent> agent = agents::make_agent(behavior);

    REQUIRE(agent != nullptr);
    CHECK(dynamic_cast<agents::MarketMakerAgent*>(agent.get()) != nullptr);
}

TEST_CASE("make_agent builds an informed trader from its spec", "[agents][agent_spec]") {
    const agents::AgentBehavior behavior = agents::InformedFlowSpec{
        .lambda = 1.0, .direction_correlation = 0.5, .size = agents::UniformIntSpec{1, 5}};

    const std::unique_ptr<agents::Agent> agent = agents::make_agent(behavior);

    REQUIRE(agent != nullptr);
    CHECK(dynamic_cast<agents::InformedFlowAgent*>(agent.get()) != nullptr);
}

TEST_CASE("make_agent builds a noise trader from its spec", "[agents][agent_spec]") {
    const agents::AgentBehavior behavior =
        agents::NoiseTraderSpec{.lambda = 1.0,
                                .size = agents::ExponentialSpec{.mean = 3.0},
                                .market_probability = 0.5,
                                .max_offset_ticks = 3};

    const std::unique_ptr<agents::Agent> agent = agents::make_agent(behavior);

    REQUIRE(agent != nullptr);
    CHECK(dynamic_cast<agents::NoiseTraderAgent*>(agent.get()) != nullptr);
}

TEST_CASE("make_agent passes on the refusal of a bad spec, whatever the kind",
          "[agents][agent_spec]") {
    const agents::AgentBehavior bad_maker = market_maker(-5);
    const agents::AgentBehavior bad_informed = agents::InformedFlowSpec{
        .lambda = -1.0, .direction_correlation = 0.5, .size = agents::UniformIntSpec{1, 5}};
    const agents::AgentBehavior bad_noise =
        agents::NoiseTraderSpec{.lambda = 1.0,
                                .size = agents::UniformIntSpec{1, 5},
                                .market_probability = 2.0,
                                .max_offset_ticks = 3};

    CHECK_THROWS_AS(agents::make_agent(bad_maker), std::invalid_argument);
    CHECK_THROWS_AS(agents::make_agent(bad_informed), std::invalid_argument);
    CHECK_THROWS_AS(agents::make_agent(bad_noise), std::invalid_argument);
}
