/// \file
/// Tests for agents/market_maker.hpp and agents/agent_spec.hpp. The market maker is driven
/// with hand-built views, with a strategy whose volatility is a constant so every quote can
/// be worked out: gamma 2, k 1, tau 1, sigma 0.2 give a half-spread of
/// ln(3)/2 + 0.04 = 0.5893 around a reservation price of mid - 0.08 * inventory, rounded
/// outwards to a tick of 0.01. What is checked is the actions it appends: what it sends on a
/// first look, what it leaves alone, what it replaces, and how it treats the orders it
/// already has, including the ones still on their way.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

#include <agents/agent_spec.hpp>
#include <agents/market_maker.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/strategy_config.hpp>

#include <math/rng/stream.hpp>
#include <math/types.hpp>

namespace {

namespace agents = mms::agents;
namespace mm = mms::mm;
namespace math = mms::math;
namespace rng = mms::math::rng;

using agents::OrderState;
using Catch::Approx;
using mm::Side;

mm::StrategyConfig strategy(std::size_t assets, mm::Quantity max_position = 1000) {
    const auto n = static_cast<Eigen::Index>(assets);
    return mm::StrategyConfig{
        .gamma = 2.0,
        .k = math::Vector::Constant(n, 1.0),
        .correlation = math::Matrix::Identity(n, n),
        .estimators = std::vector<mm::EstimatorSpec>(assets, mm::ConstantSpec{.sigma = 0.2}),
        .horizon = mm::ConstantHorizon{.tau = 1.0},
        .quote_sizes = std::vector<mm::Quantity>(assets, 10),
        .tick_sizes = std::vector<mm::Price>(assets, 0.01),
        .limits = {.max_abs_position = std::vector<mm::Quantity>(assets, max_position),
                   .max_portfolio_risk = std::nullopt},
        .min_samples = 0};
}

agents::MarketMakerSpec
spec(std::size_t assets = 1, mm::Ticks threshold = 1, mm::Quantity max_position = 1000) {
    return agents::MarketMakerSpec{.strategy = strategy(assets, max_position),
                                   .requote_threshold_ticks = threshold};
}

// Owns the buffers a view points into.
struct Scene {
    std::vector<agents::AssetView> assets;
    std::vector<mm::Quantity> positions;
    std::vector<agents::OpenOrder> orders;
    double time = 1.0;
    double elapsed = 1.0;

    explicit Scene(std::vector<double> mids) : positions(mids.size(), 0) {
        for (const double mid : mids) {
            assets.push_back(
                agents::AssetView{.bids = {}, .asks = {}, .recent_trades = {}, .signal = mid});
        }
    }

    [[nodiscard]] agents::AgentView view() const {
        return agents::AgentView{
            .tick = 1,
            .time = time,
            .elapsed = elapsed,
            .account = {.cash = 1e9, .buying_power = 1e9, .positions = positions},
            .open_orders = orders,
            .assets = assets};
    }
};

agents::OpenOrder open(mm::AssetId asset,
                       mm::OrderId id,
                       Side side,
                       double price,
                       mm::Quantity remaining,
                       OrderState state = OrderState::Resting) {
    return agents::OpenOrder{.asset = asset,
                             .id = id,
                             .side = side,
                             .price = price,
                             .remaining = remaining,
                             .state = state};
}

std::vector<agents::Action> act(agents::MarketMakerAgent& agent, const Scene& scene) {
    rng::RngEngine engine = rng::RngHub{rng::Seed{1}}.engine_for(
        {.subsystem = rng::Subsystem::Agent, .asset = rng::no_asset});
    std::vector<agents::Action> out;
    agent.act(scene.view(), engine, out);
    return out;
}

const agents::SubmitLimit*
submit_on(const std::vector<agents::Action>& actions, mm::AssetId asset, Side side) {
    for (const agents::Action& action : actions) {
        if (const auto* submit = std::get_if<agents::SubmitLimit>(&action)) {
            if (submit->asset == asset && submit->side == side) {
                return submit;
            }
        }
    }
    return nullptr;
}

std::vector<const agents::Cancel*> cancels(const std::vector<agents::Action>& actions) {
    std::vector<const agents::Cancel*> found;
    for (const agents::Action& action : actions) {
        if (const auto* cancel = std::get_if<agents::Cancel>(&action)) {
            found.push_back(cancel);
        }
    }
    return found;
}

std::size_t submit_count(const std::vector<agents::Action>& actions) {
    std::size_t count = 0;
    for (const agents::Action& action : actions) {
        count += std::holds_alternative<agents::SubmitLimit>(action) ? 1U : 0U;
    }
    return count;
}

} // namespace

// ------------------------------------------------------------------ construction

TEST_CASE("The market maker refuses a negative threshold and a strategy mm refuses",
          "[agents][market_maker]") {
    CHECK_THROWS_AS(agents::MarketMakerAgent(spec(1, -1)), std::invalid_argument);

    agents::MarketMakerSpec bad = spec();
    bad.strategy.gamma = -1.0;
    CHECK_THROWS_AS(agents::MarketMakerAgent(bad), std::invalid_argument);

    CHECK_NOTHROW(agents::MarketMakerAgent(spec(1, 0)));
}

TEST_CASE("The market maker refuses a view it cannot use", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec(2));

    Scene wrong_assets({100.0});
    CHECK_THROWS_AS(act(agent, wrong_assets), std::invalid_argument);

    Scene wrong_positions({100.0, 100.0});
    wrong_positions.positions.pop_back();
    CHECK_THROWS_AS(act(agent, wrong_positions), std::invalid_argument);

    Scene no_signal({100.0, 100.0});
    no_signal.assets[1].signal = std::nullopt;
    CHECK_THROWS_AS(act(agent, no_signal), std::invalid_argument);

    Scene no_time({100.0, 100.0});
    no_time.elapsed = 0.0;
    CHECK_THROWS_AS(act(agent, no_time), std::invalid_argument);
}

// ------------------------------------------------------------------ first quotes

TEST_CASE("With nothing in the market it posts a bid and an ask around the true price",
          "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    const Scene scene({100.0});

    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(actions.size() == 2);
    const agents::SubmitLimit* bid = submit_on(actions, 0, Side::Buy);
    const agents::SubmitLimit* ask = submit_on(actions, 0, Side::Sell);
    REQUIRE(bid != nullptr);
    REQUIRE(ask != nullptr);
    CHECK(bid->price == Approx(99.41).margin(1e-9));
    CHECK(ask->price == Approx(100.59).margin(1e-9));
    CHECK(bid->quantity == 10);
    CHECK(ask->quantity == 10);
    CHECK(bid->time_in_force == mm::TimeInForce::GTC);
    CHECK(ask->time_in_force == mm::TimeInForce::GTC);
    CHECK(cancels(actions).empty());
}

TEST_CASE("A long position moves both quotes down", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    Scene scene({100.0});
    scene.positions[0] = 10; // reservation price 100 - 2 * 0.04 * 10 = 99.2

    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(submit_on(actions, 0, Side::Buy) != nullptr);
    REQUIRE(submit_on(actions, 0, Side::Sell) != nullptr);
    CHECK(submit_on(actions, 0, Side::Buy)->price == Approx(98.61).margin(1e-9));
    CHECK(submit_on(actions, 0, Side::Sell)->price == Approx(99.79).margin(1e-9));
}

TEST_CASE("At its position limit it stops quoting the side that adds to the position",
          "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec(1, 1, 10));
    Scene scene({100.0});
    scene.positions[0] = 10;

    const std::vector<agents::Action> actions = act(agent, scene);

    CHECK(submit_on(actions, 0, Side::Buy) == nullptr);
    CHECK(submit_on(actions, 0, Side::Sell) != nullptr);
}

TEST_CASE("It quotes every asset, each around its own price", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec(2));
    const Scene scene({100.0, 50.0});

    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(actions.size() == 4);
    CHECK(submit_on(actions, 0, Side::Buy)->price == Approx(99.41).margin(1e-9));
    CHECK(submit_on(actions, 1, Side::Buy)->price == Approx(49.41).margin(1e-9));
    CHECK(submit_on(actions, 1, Side::Sell)->price == Approx(50.59).margin(1e-9));
}

// ------------------------------------------------------------------- requoting

TEST_CASE("Quotes that are still good are left alone", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    Scene scene({100.0});
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 99.41, 10),
                    open(0, mm::OrderId{2}, Side::Sell, 100.59, 10)};

    CHECK(act(agent, scene).empty());
}

TEST_CASE("A price that moved by the threshold replaces the quote: cancel, then submit",
          "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec(1, 1));
    Scene scene({100.0});
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 99.40, 10), // one tick below the wanted bid
                    open(0, mm::OrderId{2}, Side::Sell, 100.59, 10)}; // exactly right

    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(actions.size() == 2);
    const auto cancelled = cancels(actions);
    REQUIRE(cancelled.size() == 1);
    CHECK(cancelled[0]->id == mm::OrderId{1});
    CHECK(std::holds_alternative<agents::Cancel>(actions[0])); // the cancel comes first
    REQUIRE(submit_on(actions, 0, Side::Buy) != nullptr);
    CHECK(submit_on(actions, 0, Side::Buy)->price == Approx(99.41).margin(1e-9));
    CHECK(submit_on(actions, 0, Side::Sell) == nullptr);
}

TEST_CASE("A move smaller than the threshold is tolerated", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec(1, 3));
    Scene scene({100.0});
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 99.39, 10), // two ticks off
                    open(0, mm::OrderId{2}, Side::Sell, 100.59, 10)};

    CHECK(act(agent, scene).empty());
}

TEST_CASE("A threshold of zero replaces every quote at every act", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec(1, 0));
    Scene scene({100.0});
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 99.41, 10),
                    open(0, mm::OrderId{2}, Side::Sell, 100.59, 10)};

    const std::vector<agents::Action> actions = act(agent, scene);

    CHECK(cancels(actions).size() == 2);
    CHECK(submit_count(actions) == 2);
}

TEST_CASE("A quote that has been partly filled is replaced at full size",
          "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    Scene scene({100.0});
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 99.41, 4), // 6 of 10 traded
                    open(0, mm::OrderId{2}, Side::Sell, 100.59, 10)};

    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(cancels(actions).size() == 1);
    CHECK(cancels(actions)[0]->id == mm::OrderId{1});
    REQUIRE(submit_on(actions, 0, Side::Buy) != nullptr);
    CHECK(submit_on(actions, 0, Side::Buy)->quantity == 10);
}

TEST_CASE("A quote the strategy no longer wants is cancelled", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec(1, 1, 10));
    Scene scene({100.0});
    scene.positions[0] = 10; // at the limit: no more bids
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 98.61, 10),
                    open(0, mm::OrderId{2}, Side::Sell, 99.79, 10)};

    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(actions.size() == 1);
    REQUIRE(cancels(actions).size() == 1);
    CHECK(cancels(actions)[0]->id == mm::OrderId{1});
}

TEST_CASE("Only the asset that moved is touched", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec(2));
    Scene scene({100.0, 50.0});
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 99.41, 10),
                    open(0, mm::OrderId{2}, Side::Sell, 100.59, 10),
                    open(1, mm::OrderId{1}, Side::Buy, 40.0, 10), // asset 1 is far off
                    open(1, mm::OrderId{2}, Side::Sell, 50.59, 10)};

    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(cancels(actions).size() == 1);
    CHECK(cancels(actions)[0]->asset == 1);
    CHECK(cancels(actions)[0]->id == mm::OrderId{1});
    REQUIRE(submit_count(actions) == 1);
    CHECK(submit_on(actions, 1, Side::Buy) != nullptr);
}

// -------------------------------------------------------------- orders on their way

TEST_CASE("A side with an order on its way is left alone", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    Scene scene({100.0});
    scene.orders = {open(0, mm::OrderId{0}, Side::Buy, 99.41, 10, OrderState::Submitting)};

    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(actions.size() == 1); // the ask only
    CHECK(submit_on(actions, 0, Side::Sell) != nullptr);
    CHECK(submit_on(actions, 0, Side::Buy) == nullptr);
}

TEST_CASE("A side with a cancel on its way is left alone, and its quote is not cancelled again",
          "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    Scene scene({100.0});
    scene.orders = {open(0, mm::OrderId{7}, Side::Sell, 120.0, 10, OrderState::Cancelling),
                    open(0, mm::OrderId{1}, Side::Buy, 99.41, 10)};

    CHECK(act(agent, scene).empty());
}

TEST_CASE("A stale resting quote is not touched while its replacement is on its way",
          "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    Scene scene({100.0});
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 90.0, 10), // stale
                    open(0, mm::OrderId{0}, Side::Buy, 99.41, 10, OrderState::Submitting),
                    open(0, mm::OrderId{2}, Side::Sell, 100.59, 10)};

    CHECK(act(agent, scene).empty());
}

TEST_CASE("A side with two resting orders keeps the oldest and cancels the rest",
          "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    Scene scene({100.0});
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 99.41, 10),
                    open(0, mm::OrderId{2}, Side::Buy, 99.41, 10),
                    open(0, mm::OrderId{3}, Side::Buy, 99.41, 10),
                    open(0, mm::OrderId{4}, Side::Sell, 100.59, 10)};

    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(actions.size() == 2);
    const auto cancelled = cancels(actions);
    REQUIRE(cancelled.size() == 2);
    CHECK(cancelled[0]->id == mm::OrderId{2});
    CHECK(cancelled[1]->id == mm::OrderId{3});
    CHECK(submit_count(actions) == 0);
}

TEST_CASE("Two resting orders on a side are already one too many", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    Scene scene({100.0});
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 99.41, 10),
                    open(0, mm::OrderId{2}, Side::Buy, 99.41, 10),
                    open(0, mm::OrderId{3}, Side::Sell, 100.59, 10)};

    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(actions.size() == 1);
    REQUIRE(cancels(actions).size() == 1);
    CHECK(cancels(actions)[0]->id == mm::OrderId{2});
}

TEST_CASE("A crowded side is cleaned up first, even if the oldest quote is stale",
          "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    Scene scene({100.0});
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 90.0, 10), // the oldest, far from 99.41
                    open(0, mm::OrderId{2}, Side::Buy, 99.41, 10),
                    open(0, mm::OrderId{3}, Side::Sell, 100.59, 10)};

    const std::vector<agents::Action> actions = act(agent, scene);

    // Only the extra order goes; the stale one is dealt with at the next act, once the side
    // is down to one order.
    REQUIRE(actions.size() == 1);
    REQUIRE(cancels(actions).size() == 1);
    CHECK(cancels(actions)[0]->id == mm::OrderId{2});
    CHECK(submit_on(actions, 0, Side::Buy) == nullptr);
}

TEST_CASE("The extra orders are cancelled on the asset they are on", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec(2));
    Scene scene({100.0, 50.0});
    scene.orders = {open(0, mm::OrderId{1}, Side::Buy, 99.41, 10),
                    open(0, mm::OrderId{2}, Side::Sell, 100.59, 10),
                    open(1, mm::OrderId{1}, Side::Buy, 49.41, 10),
                    open(1, mm::OrderId{5}, Side::Buy, 49.41, 10), // the extra one
                    open(1, mm::OrderId{2}, Side::Sell, 50.59, 10)};

    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(actions.size() == 1);
    REQUIRE(cancels(actions).size() == 1);
    CHECK(cancels(actions)[0]->asset == 1);
    CHECK(cancels(actions)[0]->id == mm::OrderId{5});
}

// ------------------------------------------------------------------ the contract

TEST_CASE("act() appends to the buffer and does not clear it", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    const Scene scene({100.0});
    rng::RngEngine engine = rng::RngHub{rng::Seed{1}}.engine_for(
        {.subsystem = rng::Subsystem::Agent, .asset = rng::no_asset});
    std::vector<agents::Action> out;
    out.emplace_back(agents::Cancel{.asset = 9, .id = mm::OrderId{9}});

    agent.act(scene.view(), engine, out);

    REQUIRE(out.size() == 3);
    CHECK(std::holds_alternative<agents::Cancel>(out[0]));
    CHECK(std::get<agents::Cancel>(out[0]).asset == 9);
}

TEST_CASE("The market maker draws no random number", "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    const Scene scene({100.0});
    const rng::RngEngine before = rng::RngHub{rng::Seed{1}}.engine_for(
        {.subsystem = rng::Subsystem::Agent, .asset = rng::no_asset});
    rng::RngEngine engine = before;
    std::vector<agents::Action> out;

    agent.act(scene.view(), engine, out);

    CHECK(engine == before);
}

TEST_CASE("It acts again on later views: the strategy takes each new price",
          "[agents][market_maker]") {
    agents::MarketMakerAgent agent(spec());
    Scene scene({100.0});
    static_cast<void>(act(agent, scene));

    scene.time = 2.0;
    scene.assets[0].signal = 110.0;
    const std::vector<agents::Action> actions = act(agent, scene);

    REQUIRE(submit_on(actions, 0, Side::Buy) != nullptr);
    CHECK(submit_on(actions, 0, Side::Buy)->price == Approx(109.41).margin(1e-9));
}

TEST_CASE("It has no state to save yet", "[agents][market_maker]") {
    const agents::MarketMakerAgent agent(spec());

    CHECK(agent.save().empty());
}

// ---------------------------------------------------------------------- make_agent

TEST_CASE("make_agent builds a market maker from its spec", "[agents][agent_spec]") {
    const agents::AgentBehavior behavior = spec(2);

    const std::unique_ptr<agents::Agent> agent = agents::make_agent(behavior);

    REQUIRE(agent != nullptr);
    CHECK(dynamic_cast<agents::MarketMakerAgent*>(agent.get()) != nullptr);
}

TEST_CASE("make_agent passes on the refusal of a bad spec", "[agents][agent_spec]") {
    const agents::AgentBehavior behavior = spec(1, -5);

    CHECK_THROWS_AS(agents::make_agent(behavior), std::invalid_argument);
}

TEST_CASE("The agent kinds are the alternatives of AgentBehavior", "[agents][agent_spec]") {
    STATIC_REQUIRE(std::variant_size_v<agents::AgentBehavior> == 1);
    STATIC_REQUIRE(std::is_same_v<std::variant_alternative_t<0, agents::AgentBehavior>,
                                  agents::MarketMakerSpec>);
}
