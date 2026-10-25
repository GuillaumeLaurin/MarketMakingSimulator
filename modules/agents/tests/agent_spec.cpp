/// \file
/// Tests for agents/agent_spec.hpp: make_agent builds the agent each kind of spec describes,
/// and passes on the refusal of a spec the agent does not accept.

#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

#include <agents/agent_spec.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/strategy_config.hpp>

#include <math/types.hpp>

#include "scene.hpp"

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

static_assert(std::variant_size_v<agents::AgentBehavior> == 6);
static_assert(
    std::is_same_v<std::variant_alternative_t<0, agents::AgentBehavior>, agents::MarketMakerSpec>);
static_assert(
    std::is_same_v<std::variant_alternative_t<1, agents::AgentBehavior>, agents::InformedFlowSpec>);
static_assert(
    std::is_same_v<std::variant_alternative_t<2, agents::AgentBehavior>, agents::NoiseTraderSpec>);
static_assert(std::is_same_v<std::variant_alternative_t<3, agents::AgentBehavior>,
                             agents::TrendFollowerSpec>);
static_assert(
    std::is_same_v<std::variant_alternative_t<4, agents::AgentBehavior>, agents::MeanReverterSpec>);
static_assert(
    std::is_same_v<std::variant_alternative_t<5, agents::AgentBehavior>, agents::FactorTraderSpec>);

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

TEST_CASE("make_agent builds a trend follower and a mean reverter from their specs",
          "[agents][agent_spec]") {
    const agents::AgentBehavior follower = agents::TrendFollowerSpec{
        .half_life = 5.0, .threshold = 0.01, .order_size = 3, .max_position = 20};
    const agents::AgentBehavior reverter = agents::MeanReverterSpec{
        .half_life = 5.0, .threshold = 0.01, .order_size = 3, .max_position = 20};

    const std::unique_ptr<agents::Agent> a = agents::make_agent(follower);
    const std::unique_ptr<agents::Agent> b = agents::make_agent(reverter);

    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(dynamic_cast<agents::DeviationTraderAgent*>(a.get()) != nullptr);
    CHECK(dynamic_cast<agents::DeviationTraderAgent*>(b.get()) != nullptr);
}

TEST_CASE("make_agent builds a factor trader from its spec", "[agents][agent_spec]") {
    const agents::AgentBehavior behavior = agents::FactorTraderSpec{
        .sensitivities = {1.0, -0.5}, .threshold = 0.01, .order_size = 2, .max_position = 10};

    const std::unique_ptr<agents::Agent> agent = agents::make_agent(behavior);

    REQUIRE(agent != nullptr);
    CHECK(dynamic_cast<agents::FactorTraderAgent*>(agent.get()) != nullptr);
}

TEST_CASE("The follower and the reverter are the same agent with opposite stances",
          "[agents][agent_spec]") {
    // A rise of the price: the follower buys, the reverter sells. Seen through make_agent.
    const agents::AgentBehavior follower = agents::TrendFollowerSpec{
        .half_life = 1.0, .threshold = 0.01, .order_size = 3, .max_position = 20};
    const agents::AgentBehavior reverter = agents::MeanReverterSpec{
        .half_life = 1.0, .threshold = 0.01, .order_size = 3, .max_position = 20};
    const auto a = agents::make_agent(follower);
    const auto b = agents::make_agent(reverter);
    agents::testing::Scene scene(1);
    scene.quote(0, 100.0, 0.5);
    mms::math::rng::RngEngine eng = agents::testing::engine();
    std::vector<agents::Action> ignored;
    a->act(scene.view(), eng, ignored);
    b->act(scene.view(), eng, ignored);
    scene.quote(0, 110.0, 0.5);
    std::vector<agents::Action> from_follower;
    std::vector<agents::Action> from_reverter;

    a->act(scene.view(), eng, from_follower);
    b->act(scene.view(), eng, from_reverter);

    REQUIRE(from_follower.size() == 1);
    REQUIRE(from_reverter.size() == 1);
    CHECK(std::get<agents::SubmitMarket>(from_follower[0]).side == mm::Side::Buy);
    CHECK(std::get<agents::SubmitMarket>(from_reverter[0]).side == mm::Side::Sell);
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
    const agents::AgentBehavior bad_follower = agents::TrendFollowerSpec{
        .half_life = 0.0, .threshold = 0.01, .order_size = 3, .max_position = 20};
    const agents::AgentBehavior bad_reverter = agents::MeanReverterSpec{
        .half_life = 5.0, .threshold = -1.0, .order_size = 3, .max_position = 20};
    const agents::AgentBehavior bad_factor = agents::FactorTraderSpec{
        .sensitivities = {}, .threshold = 0.01, .order_size = 2, .max_position = 10};

    CHECK_THROWS_AS(agents::make_agent(bad_noise), std::invalid_argument);
    CHECK_THROWS_AS(agents::make_agent(bad_follower), std::invalid_argument);
    CHECK_THROWS_AS(agents::make_agent(bad_reverter), std::invalid_argument);
    CHECK_THROWS_AS(agents::make_agent(bad_factor), std::invalid_argument);
}
