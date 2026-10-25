/// \file
/// Tests for agents/noise_trader.hpp, driven with hand-built views. What matters: market
/// orders and passive limit orders come out in the proportion the spec says, passive prices sit
/// on the asset's own tick grid between 1 and max_offset ticks from the middle on the order's own
/// side; the draws do not depend on whether there is a book to trade against; a rejected act
/// draws nothing; and the agent keeps its passive orders under the cap by cancelling the oldest.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <variant>
#include <vector>

#include <agents/flow.hpp>
#include <agents/noise_trader.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/tick.hpp>

#include <math/dist/discrete.hpp>
#include <math/dist/uniform.hpp>

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

agents::NoiseTraderSpec spec(double lambda = 5.0,
                             double market_probability = 0.5,
                             mm::Ticks max_offset = 3,
                             std::size_t max_resting = 20) {
    return agents::NoiseTraderSpec{.lambda = lambda,
                                   .size = agents::UniformIntSpec{2, 6},
                                   .market_probability = market_probability,
                                   .max_offset_ticks = max_offset,
                                   .max_resting_orders = max_resting};
}

// A scene whose assets all have a book around `mid`.
Scene scene_around(std::vector<double> mids) {
    Scene scene(mids.size());
    for (std::size_t i = 0; i < mids.size(); ++i) {
        scene.quote(i, mids[i]);
    }
    return scene;
}

std::vector<agents::Action>
act(agents::NoiseTraderAgent& agent, Scene& scene, rng::RngEngine& eng) {
    std::vector<agents::Action> out;
    agent.act(scene.view(), eng, out);
    return out;
}

std::vector<agents::SubmitLimit> limits(const std::vector<agents::Action>& actions) {
    std::vector<agents::SubmitLimit> found;
    for (const agents::Action& action : actions) {
        if (const auto* order = std::get_if<agents::SubmitLimit>(&action)) {
            found.push_back(*order);
        }
    }
    return found;
}

std::vector<agents::SubmitMarket> markets(const std::vector<agents::Action>& actions) {
    std::vector<agents::SubmitMarket> found;
    for (const agents::Action& action : actions) {
        if (const auto* order = std::get_if<agents::SubmitMarket>(&action)) {
            found.push_back(*order);
        }
    }
    return found;
}

std::vector<agents::Cancel> cancels(const std::vector<agents::Action>& actions) {
    std::vector<agents::Cancel> found;
    for (const agents::Action& action : actions) {
        if (const auto* order = std::get_if<agents::Cancel>(&action)) {
            found.push_back(*order);
        }
    }
    return found;
}

agents::OpenOrder
open(mm::OrderId id, Side side, agents::OrderState state = agents::OrderState::Resting) {
    return agents::OpenOrder{
        .asset = 0, .id = id, .side = side, .price = 99.0, .remaining = 5, .state = state};
}

} // namespace

// ------------------------------------------------------------------ construction

TEST_CASE("The noise trader refuses a spec that makes no sense", "[agents][noise_trader]") {
    for (const double lambda : {-1.0, quiet_nan, infinity}) {
        CHECK_THROWS_AS(agents::NoiseTraderAgent(spec(lambda)), std::invalid_argument);
    }
    for (const double p : {-0.1, 1.1, quiet_nan, infinity}) {
        CHECK_THROWS_AS(agents::NoiseTraderAgent(spec(1.0, p)), std::invalid_argument);
    }
    CHECK_THROWS_AS(agents::NoiseTraderAgent(spec(1.0, 0.5, 0)), std::invalid_argument);
    CHECK_THROWS_AS(agents::NoiseTraderAgent(spec(1.0, 0.5, -3)), std::invalid_argument);

    agents::NoiseTraderSpec bad_size = spec();
    bad_size.size = agents::UniformIntSpec{0, 4};
    CHECK_THROWS_AS(agents::NoiseTraderAgent(bad_size), std::invalid_argument);

    CHECK_NOTHROW(agents::NoiseTraderAgent(spec(0.0, 0.0, 1, 0)));
    CHECK_NOTHROW(agents::NoiseTraderAgent(spec(1.0, 1.0)));
}

// --------------------------------------------------------------------- rejections

TEST_CASE("A view it cannot use is refused before anything is drawn", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec());
    rng::RngEngine eng = engine();
    const rng::RngEngine before = eng;

    for (const double tick : {0.0, -0.01, quiet_nan, infinity}) {
        Scene scene = scene_around({100.0, 50.0});
        scene.markets[1].tick_size = tick;
        std::vector<agents::Action> out;
        CHECK_THROWS_AS(agent.act(scene.view(), eng, out), std::invalid_argument);
    }
    for (const double elapsed : {0.0, -1.0, quiet_nan, infinity}) {
        Scene scene = scene_around({100.0});
        scene.elapsed = elapsed;
        std::vector<agents::Action> out;
        CHECK_THROWS_AS(agent.act(scene.view(), eng, out), std::invalid_argument);
    }
    agents::NoiseTraderAgent hasty(spec(1e5));
    Scene long_wait = scene_around({100.0});
    long_wait.elapsed = 100.0;
    std::vector<agents::Action> out;
    CHECK_THROWS_AS(hasty.act(long_wait.view(), eng, out), std::invalid_argument);

    CHECK(eng == before);
}

// -------------------------------------------------------------------------- orders

TEST_CASE("With a rate of zero it sends nothing and draws nothing", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(0.0));
    rng::RngEngine eng = engine();
    const rng::RngEngine before = eng;
    Scene scene = scene_around({100.0});

    CHECK(act(agent, scene, eng).empty());
    CHECK(eng == before);
}

TEST_CASE("With a probability of one every order is an immediate market order",
          "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(20.0, 1.0));
    rng::RngEngine eng = engine(2);
    Scene scene = scene_around({100.0});

    const std::vector<agents::Action> actions = act(agent, scene, eng);

    REQUIRE(actions.size() > 5);
    CHECK(limits(actions).empty());
    for (const agents::SubmitMarket& order : markets(actions)) {
        CHECK(order.asset == 0);
        CHECK(order.time_in_force == mm::MarketTimeInForce::IOC);
        CHECK(order.quantity >= 2);
        CHECK(order.quantity <= 6);
    }
}

TEST_CASE("With a probability of zero every order is a passive limit order on the tick grid",
          "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(30.0, 0.0, 4));
    rng::RngEngine eng = engine(3);
    Scene scene = scene_around({100.0});

    const std::vector<agents::Action> actions = act(agent, scene, eng);

    const auto orders = limits(actions);
    REQUIRE(orders.size() > 10);
    CHECK(markets(actions).empty());
    bool saw_buy = false;
    bool saw_sell = false;
    for (const agents::SubmitLimit& order : orders) {
        CHECK(order.time_in_force == mm::TimeInForce::GTC);
        CHECK(order.quantity >= 2);
        CHECK(order.quantity <= 6);
        const mm::Ticks ticks = mm::nearest_ticks(order.price, 0.01);
        CHECK(order.price == mm::price_at(ticks, 0.01)); // exactly on the grid
        if (order.side == Side::Buy) {
            saw_buy = true;
            CHECK(ticks >= 10000 - 4); // 1 to 4 ticks below the middle, 100.00
            CHECK(ticks <= 10000 - 1);
        } else {
            saw_sell = true;
            CHECK(ticks >= 10000 + 1);
            CHECK(ticks <= 10000 + 4);
        }
    }
    CHECK(saw_buy);
    CHECK(saw_sell);
}

TEST_CASE("Every offset from one tick to the maximum is used", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(200.0, 0.0, 3));
    rng::RngEngine eng = engine(4);
    Scene scene = scene_around({100.0});

    std::set<mm::Ticks> offsets;
    for (const agents::SubmitLimit& order : limits(act(agent, scene, eng))) {
        offsets.insert(std::llabs(mm::nearest_ticks(order.price, 0.01) - 10000));
    }

    CHECK(offsets == std::set<mm::Ticks>{1, 2, 3});
}

TEST_CASE("With a maximum offset of one every passive order is exactly one tick off the middle",
          "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(50.0, 0.0, 1));
    rng::RngEngine eng = engine(5);
    Scene scene = scene_around({100.0});

    for (const agents::SubmitLimit& order : limits(act(agent, scene, eng))) {
        CHECK(order.price ==
              (order.side == Side::Buy ? mm::price_at(9999, 0.01) : mm::price_at(10001, 0.01)));
    }
}

TEST_CASE("The share of market orders follows the probability", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(500.0, 0.3));
    rng::RngEngine eng = engine(6);
    Scene scene = scene_around({100.0});

    const std::vector<agents::Action> actions = act(agent, scene, eng);

    const double share =
        static_cast<double>(markets(actions).size()) / static_cast<double>(actions.size());
    CHECK(share > 0.22);
    CHECK(share < 0.38);
}

TEST_CASE("The sides are a fair coin", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(500.0, 1.0));
    rng::RngEngine eng = engine(7);
    Scene scene = scene_around({100.0});

    double buys = 0.0;
    const auto orders = markets(act(agent, scene, eng));
    for (const agents::SubmitMarket& order : orders) {
        buys += order.side == Side::Buy ? 1.0 : 0.0;
    }

    CHECK(buys / static_cast<double>(orders.size()) > 0.42);
    CHECK(buys / static_cast<double>(orders.size()) < 0.58);
}

TEST_CASE("Each asset is priced on its own grid", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(100.0, 0.0, 2));
    rng::RngEngine eng = engine(8);
    Scene scene = scene_around({100.0, 40.0});
    scene.markets[1].tick_size = 0.5;

    const auto orders = limits(act(agent, scene, eng));

    bool saw_second = false;
    for (const agents::SubmitLimit& order : orders) {
        if (order.asset == 1) {
            saw_second = true;
            const mm::Ticks ticks = mm::nearest_ticks(order.price, 0.5);
            CHECK(order.price == mm::price_at(ticks, 0.5));
            CHECK(std::llabs(ticks - 80) <= 2); // 40 / 0.5 = 80 ticks
        }
    }
    CHECK(saw_second);
}

TEST_CASE("A price of zero or less is not an order", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(300.0, 0.0, 5));
    rng::RngEngine eng = engine(9);
    Scene scene(1);
    scene.markets[0].bids = {agents::BookLevel{.price = 0.015, .quantity = 10}};
    scene.markets[0].asks = {agents::BookLevel{.price = 0.025, .quantity = 10}}; // middle 0.02

    const auto orders = limits(act(agent, scene, eng));

    REQUIRE_FALSE(orders.empty());
    for (const agents::SubmitLimit& order : orders) {
        CHECK(order.price > 0.0);
    }
}

TEST_CASE("Without a two-sided book it goes by the last trade", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(100.0, 0.0, 1));
    rng::RngEngine eng = engine(10);
    Scene scene(1);
    scene.markets[0].trades = {
        agents::TradePrint{.price = 60.0, .quantity = 1, .aggressor = Side::Buy}};

    const auto orders = limits(act(agent, scene, eng));

    REQUIRE_FALSE(orders.empty());
    for (const agents::SubmitLimit& order : orders) {
        CHECK(order.price ==
              (order.side == Side::Buy ? mm::price_at(5999, 0.01) : mm::price_at(6001, 0.01)));
    }
}

TEST_CASE("An asset with no price to read is left alone, market orders included",
          "[agents][noise_trader]") {
    // The noise trader reads the price of every asset first; with no price it does nothing on
    // that asset, market orders included (it would be trading blind).
    agents::NoiseTraderAgent agent(spec(100.0, 1.0));
    rng::RngEngine eng = engine(11);
    Scene scene(2);
    scene.quote(1, 50.0); // asset 0 has no book and no trade

    const std::vector<agents::Action> actions = act(agent, scene, eng);

    REQUIRE_FALSE(actions.empty());
    for (const agents::SubmitMarket& order : markets(actions)) {
        CHECK(order.asset == 1);
    }
}

// ------------------------------------------------------------------------- draws

TEST_CASE("The draws follow the arrivals, not the state of the book", "[agents][noise_trader]") {
    const agents::NoiseTraderSpec s = spec(10.0, 0.5, 3);
    agents::NoiseTraderAgent with_book(s);
    agents::NoiseTraderAgent without_book(s);
    rng::RngEngine eng_a = engine(12);
    rng::RngEngine eng_b = engine(12);
    Scene booked = scene_around({100.0, 50.0});
    Scene empty(2);

    const std::vector<agents::Action> a = act(with_book, booked, eng_a);
    const std::vector<agents::Action> b = act(without_book, empty, eng_b);

    CHECK_FALSE(a.empty());
    CHECK(b.empty());
    CHECK(eng_a == eng_b); // the same number of words were drawn
}

TEST_CASE("It draws exactly the documented words for each arrival", "[agents][noise_trader]") {
    const agents::NoiseTraderSpec s = spec(8.0, 0.4, 3);
    agents::NoiseTraderAgent agent(s);
    rng::RngEngine eng = engine(13);
    rng::RngEngine reference = eng;
    Scene scene = scene_around({100.0, 50.0});
    scene.elapsed = 1.5;

    const std::vector<agents::Action> actions = act(agent, scene, eng);

    // The same draws by hand: per asset, the count; per arrival, the direction, the size, the
    // kind and the offset.
    std::size_t expected_orders = 0;
    for (std::size_t asset = 0; asset < 2; ++asset) {
        const std::int64_t count = agents::draw_arrival_count(s.lambda, 1.5, reference);
        for (std::int64_t i = 0; i < count; ++i) {
            static_cast<void>(agents::draw_direction(0.0, 0.0, reference));
            static_cast<void>(agents::draw_size(s.size, reference));
            static_cast<void>(mms::math::dist::bernoulli(reference, s.market_probability));
            static_cast<void>(mms::math::dist::uniform_int(reference, 1, s.max_offset_ticks));
            ++expected_orders;
        }
    }
    CHECK(eng == reference);
    CHECK(actions.size() == expected_orders); // each arrival with a book gives one order
}

TEST_CASE("The same engine state gives the same orders", "[agents][noise_trader]") {
    agents::NoiseTraderAgent first(spec());
    agents::NoiseTraderAgent second(spec());
    rng::RngEngine eng_a = engine(14);
    rng::RngEngine eng_b = engine(14);
    Scene scene = scene_around({100.0});

    const auto a = act(first, scene, eng_a);
    const auto b = act(second, scene, eng_b);

    REQUIRE(a.size() == b.size());
    REQUIRE_FALSE(a.empty());
    CHECK(limits(a).size() == limits(b).size());
    for (std::size_t i = 0; i < limits(a).size(); ++i) {
        CHECK(limits(a)[i].price == limits(b)[i].price);
        CHECK(limits(a)[i].side == limits(b)[i].side);
        CHECK(limits(a)[i].quantity == limits(b)[i].quantity);
    }
}

TEST_CASE("The noise trader has no state to save", "[agents][noise_trader]") {
    const agents::NoiseTraderAgent agent(spec());

    CHECK(agent.save().empty());
}

// -------------------------------------------------------------------- the cap

TEST_CASE("Over its cap it cancels the oldest resting orders first", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(0.0, 0.5, 3, 3));
    rng::RngEngine eng = engine();
    Scene scene = scene_around({100.0});
    scene.orders = {open(mm::OrderId{1}, Side::Buy),
                    open(mm::OrderId{2}, Side::Sell),
                    open(mm::OrderId{3}, Side::Buy),
                    open(mm::OrderId{4}, Side::Sell),
                    open(mm::OrderId{5}, Side::Buy)};

    const auto gone = cancels(act(agent, scene, eng));

    REQUIRE(gone.size() == 2);
    CHECK(gone[0].id == mm::OrderId{1});
    CHECK(gone[1].id == mm::OrderId{2});
    CHECK(gone[0].asset == 0);
}

TEST_CASE("The orders it cancels are named with their own asset", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(0.0, 0.5, 3, 1));
    rng::RngEngine eng = engine();
    Scene scene = scene_around({100.0, 50.0});
    scene.orders = {agents::OpenOrder{.asset = 1,
                                      .id = mm::OrderId{7},
                                      .side = Side::Buy,
                                      .price = 49.0,
                                      .remaining = 5,
                                      .state = agents::OrderState::Resting},
                    agents::OpenOrder{.asset = 0,
                                      .id = mm::OrderId{8},
                                      .side = Side::Sell,
                                      .price = 101.0,
                                      .remaining = 5,
                                      .state = agents::OrderState::Resting}};

    const auto gone = cancels(act(agent, scene, eng));

    REQUIRE(gone.size() == 1);
    CHECK(gone[0].asset == 1); // the oldest is the one on the second asset
    CHECK(gone[0].id == mm::OrderId{7});
}

TEST_CASE("At or under its cap it cancels nothing", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(0.0, 0.5, 3, 3));
    rng::RngEngine eng = engine();
    Scene scene = scene_around({100.0});
    scene.orders = {open(mm::OrderId{1}, Side::Buy),
                    open(mm::OrderId{2}, Side::Sell),
                    open(mm::OrderId{3}, Side::Buy)};

    CHECK(cancels(act(agent, scene, eng)).empty());
}

TEST_CASE("Orders already being cancelled do not count towards the cap", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(0.0, 0.5, 3, 2));
    rng::RngEngine eng = engine();
    Scene scene = scene_around({100.0});
    scene.orders = {open(mm::OrderId{1}, Side::Buy),
                    open(mm::OrderId{2}, Side::Sell),
                    open(mm::OrderId{3}, Side::Buy, agents::OrderState::Cancelling),
                    open(mm::OrderId{4}, Side::Sell, agents::OrderState::Cancelling)};

    CHECK(cancels(act(agent, scene, eng)).empty());
}

TEST_CASE("Orders still on their way count towards the cap but cannot be cancelled",
          "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(0.0, 0.5, 3, 2));
    rng::RngEngine eng = engine();
    Scene scene = scene_around({100.0});
    scene.orders = {open(mm::OrderId{1}, Side::Buy),
                    open(mm::OrderId{0}, Side::Sell, agents::OrderState::Submitting),
                    open(mm::OrderId{0}, Side::Buy, agents::OrderState::Submitting),
                    open(mm::OrderId{0}, Side::Sell, agents::OrderState::Submitting)};

    const auto gone = cancels(act(agent, scene, eng));

    REQUIRE(gone.size() == 1); // two too many, but only one of them can be cancelled
    CHECK(gone[0].id == mm::OrderId{1});
}

TEST_CASE("The new passive orders count towards the cap too", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(40.0, 0.0, 3, 5));
    rng::RngEngine eng = engine(15);
    Scene scene = scene_around({100.0});
    scene.orders = {open(mm::OrderId{1}, Side::Buy),
                    open(mm::OrderId{2}, Side::Sell),
                    open(mm::OrderId{3}, Side::Buy)};

    const std::vector<agents::Action> actions = act(agent, scene, eng);

    const std::size_t added = limits(actions).size();
    REQUIRE(added > 5);
    // 3 open + added - 5 over the cap, but only the 3 resting orders can go.
    CHECK(cancels(actions).size() == std::min<std::size_t>(3, 3 + added - 5));
}

TEST_CASE("A cap of zero keeps no passive order at all", "[agents][noise_trader]") {
    agents::NoiseTraderAgent agent(spec(0.0, 0.0, 3, 0));
    rng::RngEngine eng = engine();
    Scene scene = scene_around({100.0});
    scene.orders = {open(mm::OrderId{1}, Side::Buy), open(mm::OrderId{2}, Side::Sell)};

    CHECK(cancels(act(agent, scene, eng)).size() == 2);
}
