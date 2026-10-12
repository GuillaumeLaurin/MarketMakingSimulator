/// \file
/// Tests for agents/factor_trader.hpp, driven with hand-built views. The score is the sum of the
/// factor moves weighted by the agent's sensitivities; what is checked is which way and when it
/// trades, that a zero sensitivity ignores a factor and a negative one bets against it, that the
/// position limit cuts each asset's order on its own, and that a view without the right factors is
/// refused.

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <variant>
#include <vector>

#include <agents/factor_trader.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scene.hpp"

namespace {

namespace agents = mms::agents;
namespace mm = mms::mm;
namespace rng = mms::math::rng;

using agents::testing::engine;
using agents::testing::Scene;
using mm::Side;

constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();

agents::FactorTraderSpec spec(std::vector<double> sensitivities = {1.0},
                              double threshold = 0.01,
                              mm::Quantity order_size = 4,
                              mm::Quantity max_position = 10) {
    return agents::FactorTraderSpec{.sensitivities = std::move(sensitivities),
                                    .threshold = threshold,
                                    .order_size = order_size,
                                    .max_position = max_position};
}

Scene scene_with(std::size_t assets, std::vector<double> factors) {
    Scene scene(assets);
    scene.factors = std::move(factors);
    return scene;
}

std::vector<agents::Action> act(agents::FactorTraderAgent& agent, Scene& scene) {
    rng::RngEngine eng = engine();
    std::vector<agents::Action> out;
    agent.act(scene.view(), eng, out);
    return out;
}

} // namespace

// ------------------------------------------------------------------ construction

TEST_CASE("The factor trader refuses a spec that makes no sense", "[agents][factor_trader]") {
    CHECK_THROWS_AS(agents::FactorTraderAgent(spec({})), std::invalid_argument);
    CHECK_THROWS_AS(agents::FactorTraderAgent(spec({1.0, quiet_nan})), std::invalid_argument);
    CHECK_THROWS_AS(agents::FactorTraderAgent(spec({infinity})), std::invalid_argument);
    for (const double threshold : {-0.1, quiet_nan, infinity}) {
        CHECK_THROWS_AS(agents::FactorTraderAgent(spec({1.0}, threshold)), std::invalid_argument);
    }
    CHECK_THROWS_AS(agents::FactorTraderAgent(spec({1.0}, 0.01, 0)), std::invalid_argument);
    CHECK_THROWS_AS(agents::FactorTraderAgent(spec({1.0}, 0.01, 4, 0)), std::invalid_argument);

    CHECK_NOTHROW(agents::FactorTraderAgent(spec({0.0, -1.0, 2.5}, 0.0, 1, 1)));
}

// --------------------------------------------------------------------- rejections

TEST_CASE("A view without one move per sensitivity is refused", "[agents][factor_trader]") {
    agents::FactorTraderAgent agent(spec({1.0, 2.0}));

    Scene none = scene_with(1, {}); // an agent that was not given the factors
    Scene one = scene_with(1, {0.1});
    Scene three = scene_with(1, {0.1, 0.2, 0.3});

    CHECK_THROWS_AS(act(agent, none), std::invalid_argument);
    CHECK_THROWS_AS(act(agent, one), std::invalid_argument);
    CHECK_THROWS_AS(act(agent, three), std::invalid_argument);
}

TEST_CASE("A move that is not a number is refused", "[agents][factor_trader]") {
    agents::FactorTraderAgent agent(spec({1.0, 1.0}));
    Scene nan_move = scene_with(1, {0.1, quiet_nan});
    Scene inf_move = scene_with(1, {infinity, 0.1});

    CHECK_THROWS_AS(act(agent, nan_move), std::invalid_argument);
    CHECK_THROWS_AS(act(agent, inf_move), std::invalid_argument);
}

TEST_CASE("Every asset needs a position", "[agents][factor_trader]") {
    agents::FactorTraderAgent agent(spec());
    Scene scene = scene_with(2, {0.5});
    scene.positions.pop_back();

    CHECK_THROWS_AS(act(agent, scene), std::invalid_argument);
}

// ------------------------------------------------------------------------- score

TEST_CASE("A positive score buys every asset, a negative one sells every asset",
          "[agents][factor_trader]") {
    agents::FactorTraderAgent agent(spec({1.0}, 0.01, 4, 10));
    Scene up = scene_with(3, {0.05});
    Scene down = scene_with(3, {-0.05});

    const auto buys = act(agent, up);
    const auto sells = act(agent, down);

    REQUIRE(buys.size() == 3);
    REQUIRE(sells.size() == 3);
    for (std::size_t asset = 0; asset < 3; ++asset) {
        const auto& buy = std::get<agents::SubmitMarket>(buys[asset]);
        const auto& sell = std::get<agents::SubmitMarket>(sells[asset]);
        CHECK(buy.asset == asset);
        CHECK(buy.side == Side::Buy);
        CHECK(buy.quantity == 4);
        CHECK(buy.time_in_force == mm::MarketTimeInForce::IOC);
        CHECK(sell.asset == asset);
        CHECK(sell.side == Side::Sell);
    }
}

TEST_CASE("A score inside the threshold, or exactly on it, does not trade",
          "[agents][factor_trader]") {
    agents::FactorTraderAgent agent(spec({1.0}, 0.5));
    Scene inside = scene_with(1, {0.25});
    Scene on_it = scene_with(1, {0.5});
    Scene just_past = scene_with(1, {0.5000001});
    Scene negative_on_it = scene_with(1, {-0.5});

    CHECK(act(agent, inside).empty());
    CHECK(act(agent, on_it).empty());
    CHECK(act(agent, negative_on_it).empty());
    CHECK(act(agent, just_past).size() == 1);
}

TEST_CASE("The score weighs each factor by its sensitivity", "[agents][factor_trader]") {
    // 1.0 * 0.03 + (-2.0) * 0.01 = 0.01
    agents::FactorTraderAgent trades(spec({1.0, -2.0}, 0.005));
    agents::FactorTraderAgent does_not(spec({1.0, -2.0}, 0.02));
    Scene scene = scene_with(1, {0.03, 0.01});

    const auto actions = act(trades, scene);
    REQUIRE(actions.size() == 1);
    CHECK(std::get<agents::SubmitMarket>(actions[0]).side == Side::Buy);
    CHECK(act(does_not, scene).empty());
}

TEST_CASE("A negative sensitivity bets against its factor", "[agents][factor_trader]") {
    agents::FactorTraderAgent against(spec({-1.0}));
    Scene rise = scene_with(1, {0.1});

    const auto actions = act(against, rise);

    REQUIRE(actions.size() == 1);
    CHECK(std::get<agents::SubmitMarket>(actions[0]).side == Side::Sell);
}

TEST_CASE("A sensitivity of zero ignores its factor", "[agents][factor_trader]") {
    agents::FactorTraderAgent agent(spec({0.0, 1.0}, 0.01));
    Scene only_the_ignored = scene_with(1, {10.0, 0.0});
    Scene only_the_followed = scene_with(1, {0.0, 0.05});

    CHECK(act(agent, only_the_ignored).empty());
    CHECK(act(agent, only_the_followed).size() == 1);
}

// ---------------------------------------------------------------------- positions

TEST_CASE("The position limit cuts each asset's order on its own", "[agents][factor_trader]") {
    agents::FactorTraderAgent agent(spec({1.0}, 0.01, 4, 10));
    Scene scene = scene_with(3, {0.1});
    scene.positions = {0, 8, 10}; // room for 10, for 2, for none

    const auto actions = act(agent, scene);

    REQUIRE(actions.size() == 2);
    const auto& first = std::get<agents::SubmitMarket>(actions[0]);
    const auto& second = std::get<agents::SubmitMarket>(actions[1]);
    CHECK(first.asset == 0);
    CHECK(first.quantity == 4);
    CHECK(second.asset == 1);
    CHECK(second.quantity == 2);
}

TEST_CASE("Selling is cut by a short position the same way", "[agents][factor_trader]") {
    agents::FactorTraderAgent agent(spec({1.0}, 0.01, 4, 10));
    Scene scene = scene_with(2, {-0.1});
    scene.positions = {-9, 10}; // room to sell 1, and room to sell all 4

    const auto actions = act(agent, scene);

    REQUIRE(actions.size() == 2);
    CHECK(std::get<agents::SubmitMarket>(actions[0]).quantity == 1);
    CHECK(std::get<agents::SubmitMarket>(actions[1]).quantity == 4);
}

// ------------------------------------------------------------------------ contract

TEST_CASE("It draws no random number, and keeps no state", "[agents][factor_trader]") {
    agents::FactorTraderAgent agent(spec());
    Scene scene = scene_with(1, {0.2});
    rng::RngEngine eng = engine();
    const rng::RngEngine before = eng;
    std::vector<agents::Action> out;

    agent.act(scene.view(), eng, out);

    CHECK(eng == before);
    CHECK(out.size() == 1);
    CHECK(agent.save().empty());
}

TEST_CASE("act() appends to the buffer and does not clear it", "[agents][factor_trader]") {
    agents::FactorTraderAgent agent(spec());
    Scene scene = scene_with(1, {0.2});
    rng::RngEngine eng = engine();
    std::vector<agents::Action> out;
    out.emplace_back(agents::Cancel{.asset = 9, .id = mm::OrderId{9}});

    agent.act(scene.view(), eng, out);

    CHECK(out.size() == 2);
    CHECK(std::holds_alternative<agents::Cancel>(out[0]));
}

TEST_CASE("The same view gives the same orders every time", "[agents][factor_trader]") {
    agents::FactorTraderAgent agent(spec({1.0, 0.5}, 0.0));
    Scene scene = scene_with(2, {0.03, -0.01});

    const auto first = act(agent, scene);
    const auto second = act(agent, scene);

    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(std::get<agents::SubmitMarket>(first[i]).quantity ==
              std::get<agents::SubmitMarket>(second[i]).quantity);
        CHECK(std::get<agents::SubmitMarket>(first[i]).side ==
              std::get<agents::SubmitMarket>(second[i]).side);
    }
}
