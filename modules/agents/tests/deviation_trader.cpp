/// \file
/// Tests for agents/deviation_trader.hpp: the trend follower and the mean reverter. The agent is
/// driven with hand-built books, with the averaging made easy to follow: a half-life of one act
/// gives a weight of one half, so an average of 100 that sees 102 becomes 101. What is checked is
/// when it trades and which way for each stance, how big, how the position limit cuts it, how the
/// half-life changes the average, and that the averages are saved and make a difference.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <variant>
#include <vector>

#include <agents/deviation_trader.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scene.hpp"

namespace {

namespace agents = mms::agents;
namespace mm = mms::mm;
namespace rng = mms::math::rng;

using agents::Stance;
using agents::testing::engine;
using agents::testing::Scene;
using mm::Side;

constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();

agents::DeviationParams params(double half_life = 1.0,
                               double threshold = 0.01,
                               mm::Quantity order_size = 5,
                               mm::Quantity max_position = 12) {
    return agents::DeviationParams{.half_life = half_life,
                                   .threshold = threshold,
                                   .order_size = order_size,
                                   .max_position = max_position};
}

// A scene whose assets all have a book around the given prices.
Scene scene_around(std::vector<double> mids) {
    Scene scene(mids.size());
    for (std::size_t i = 0; i < mids.size(); ++i) {
        scene.quote(i, mids[i], 0.5);
    }
    return scene;
}

void set_prices(Scene& scene, std::vector<double> mids) {
    for (std::size_t i = 0; i < mids.size(); ++i) {
        scene.quote(i, mids[i], 0.5);
    }
}

std::vector<agents::Action> act(agents::DeviationTraderAgent& agent, Scene& scene) {
    rng::RngEngine eng = engine();
    std::vector<agents::Action> out;
    agent.act(scene.view(), eng, out);
    return out;
}

const agents::SubmitMarket& only(const std::vector<agents::Action>& actions) {
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<agents::SubmitMarket>(actions[0]));
    return std::get<agents::SubmitMarket>(actions[0]);
}

} // namespace

// ------------------------------------------------------------------ construction

TEST_CASE("The deviation trader refuses parameters that make no sense", "[agents][deviation]") {
    for (const double half_life : {0.0, -1.0, quiet_nan, infinity}) {
        CHECK_THROWS_AS(agents::DeviationTraderAgent(params(half_life), Stance::Follow),
                        std::invalid_argument);
    }
    for (const double threshold : {-0.01, quiet_nan, infinity}) {
        CHECK_THROWS_AS(agents::DeviationTraderAgent(params(1.0, threshold), Stance::Fade),
                        std::invalid_argument);
    }
    CHECK_THROWS_AS(agents::DeviationTraderAgent(params(1.0, 0.01, 0), Stance::Follow),
                    std::invalid_argument);
    CHECK_THROWS_AS(agents::DeviationTraderAgent(params(1.0, 0.01, 5, 0), Stance::Follow),
                    std::invalid_argument);

    CHECK_NOTHROW(agents::DeviationTraderAgent(params(1.0, 0.0, 1, 1), Stance::Fade));
}

TEST_CASE("The first price only starts the average", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(), Stance::Follow);
    Scene scene = scene_around({100.0});

    CHECK(act(agent, scene).empty());
}

// ------------------------------------------------------------------------ stance

TEST_CASE("A trend follower buys a rise and sells a fall", "[agents][deviation]") {
    agents::DeviationTraderAgent up(params(), Stance::Follow);
    agents::DeviationTraderAgent down(params(), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(up, scene));
    static_cast<void>(act(down, scene));

    set_prices(scene, {102.0}); // +2 %, over the 1 % threshold
    const auto after_rise = act(up, scene);
    set_prices(scene, {98.0}); // the other agent saw 100 before: -2 %
    const auto after_fall = act(down, scene);

    CHECK(only(after_rise).side == Side::Buy);
    CHECK(only(after_rise).quantity == 5);
    CHECK(only(after_rise).time_in_force == mm::MarketTimeInForce::IOC);
    CHECK(only(after_fall).side == Side::Sell);
}

TEST_CASE("A mean reverter sells a rise and buys a fall", "[agents][deviation]") {
    agents::DeviationTraderAgent up(params(), Stance::Fade);
    agents::DeviationTraderAgent down(params(), Stance::Fade);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(up, scene));
    static_cast<void>(act(down, scene));

    set_prices(scene, {102.0});
    const auto after_rise = act(up, scene);
    set_prices(scene, {98.0});
    const auto after_fall = act(down, scene);

    CHECK(only(after_rise).side == Side::Sell);
    CHECK(only(after_fall).side == Side::Buy);
}

TEST_CASE("A move inside the threshold is no reason to trade", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(agent, scene));

    set_prices(scene, {100.5}); // +0.5 %

    CHECK(act(agent, scene).empty());
}

TEST_CASE("A deviation exactly at the threshold does not trade; just past it does",
          "[agents][deviation]") {
    // 65 / 64 - 1 = 1/64, exactly representable, so the boundary is exact.
    constexpr double one_sixty_fourth = 0.015625;
    agents::DeviationTraderAgent at_it(params(1.0, one_sixty_fourth), Stance::Follow);
    agents::DeviationTraderAgent below_it(params(1.0, one_sixty_fourth / 2.0), Stance::Follow);
    Scene scene = scene_around({64.0});
    static_cast<void>(act(at_it, scene));
    static_cast<void>(act(below_it, scene));

    set_prices(scene, {65.0});

    CHECK(act(at_it, scene).empty());
    CHECK(act(below_it, scene).size() == 1);
}

TEST_CASE("With a threshold of zero any move trades", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(1.0, 0.0), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(agent, scene));

    set_prices(scene, {100.5});

    CHECK(only(act(agent, scene)).side == Side::Buy);
}

TEST_CASE("A price that has not moved does not trade even at a threshold of zero",
          "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(1.0, 0.0), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(agent, scene));

    CHECK(act(agent, scene).empty());
}

// ----------------------------------------------------------------------- average

TEST_CASE("A half-life of one act gives the new price a weight of one half",
          "[agents][deviation]") {
    // 100 then 102: the average becomes 101. The same 102 again is then 102 / 101 - 1 = 0.009901
    // above it, which a threshold of 0.0099 trades and one of 0.00991 does not. Any other
    // weight would put the average elsewhere and both would trade.
    agents::DeviationTraderAgent trades(params(1.0, 0.0099), Stance::Follow);
    agents::DeviationTraderAgent does_not(params(1.0, 0.00991), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(trades, scene));
    static_cast<void>(act(does_not, scene));

    set_prices(scene, {102.0}); // +2 %: both trade, and both averages become 101
    REQUIRE(act(trades, scene).size() == 1);
    REQUIRE(act(does_not, scene).size() == 1);

    CHECK(act(trades, scene).size() == 1);
    CHECK(act(does_not, scene).empty());
}

TEST_CASE("The half-life sets how fast the average follows", "[agents][deviation]") {
    agents::DeviationTraderAgent quick(params(1.0, 0.05), Stance::Follow);
    agents::DeviationTraderAgent slow(params(2.0, 0.05), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(quick, scene));
    static_cast<void>(act(slow, scene));

    set_prices(scene, {110.0}); // both see +10 %, over the threshold
    REQUIRE(act(quick, scene).size() == 1);
    REQUIRE(act(slow, scene).size() == 1);

    // The quick average is now 105 (110 / 105 = +4.8 %, under 5 %). The slow one has only moved
    // to 102.93 (110 / 102.93 = +6.9 %), so it still sees a rise.
    CHECK(act(quick, scene).empty());
    CHECK(act(slow, scene).size() == 1);
}

// ---------------------------------------------------------------------- positions

TEST_CASE("The position limit cuts the order down", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(1.0, 0.01, 5, 12), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(agent, scene));
    scene.positions[0] = 10; // room for 2 more

    set_prices(scene, {105.0});

    CHECK(only(act(agent, scene)).quantity == 2);
}

TEST_CASE("At the limit it does not buy more, but it still sells", "[agents][deviation]") {
    agents::DeviationTraderAgent follower(params(1.0, 0.01, 5, 12), Stance::Follow);
    agents::DeviationTraderAgent reverter(params(1.0, 0.01, 5, 12), Stance::Fade);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(follower, scene));
    static_cast<void>(act(reverter, scene));
    scene.positions[0] = 12;

    set_prices(scene, {105.0}); // a rise: the follower would buy, the reverter sell

    CHECK(act(follower, scene).empty());
    CHECK(only(act(reverter, scene)).side == Side::Sell);
}

TEST_CASE("A short position at the limit does not sell more", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(1.0, 0.01, 5, 12), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(agent, scene));
    scene.positions[0] = -12;

    set_prices(scene, {95.0}); // a fall: it would sell

    CHECK(act(agent, scene).empty());
}

// ------------------------------------------------------------------------ assets

TEST_CASE("Each asset has its own average and its own orders", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(), Stance::Follow);
    Scene scene = scene_around({100.0, 50.0, 20.0});
    static_cast<void>(act(agent, scene));

    set_prices(scene, {105.0, 50.0, 18.0}); // up, flat, down
    const auto actions = act(agent, scene);

    REQUIRE(actions.size() == 2);
    const auto& first = std::get<agents::SubmitMarket>(actions[0]);
    const auto& second = std::get<agents::SubmitMarket>(actions[1]);
    CHECK(first.asset == 0);
    CHECK(first.side == Side::Buy);
    CHECK(second.asset == 2);
    CHECK(second.side == Side::Sell);
}

TEST_CASE("A middle of zero or less is not a price, and is left alone", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(1.0, 0.0), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(agent, scene));

    // A bid of -1 and an ask of 1: the middle is exactly zero. Then a negative middle.
    scene.markets[0].bids = {agents::BookLevel{.price = -1.0, .quantity = 10}};
    scene.markets[0].asks = {agents::BookLevel{.price = 1.0, .quantity = 10}};
    CHECK(act(agent, scene).empty());
    scene.markets[0].bids = {agents::BookLevel{.price = -5.0, .quantity = 10}};
    scene.markets[0].asks = {agents::BookLevel{.price = -3.0, .quantity = 10}};
    CHECK(act(agent, scene).empty());

    // And the average it kept is still the one from before: a real rise still trades.
    set_prices(scene, {110.0});
    CHECK(only(act(agent, scene)).side == Side::Buy);
}

TEST_CASE("An asset with no price is left alone, and its average waits", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(), Stance::Follow);
    Scene scene(2);
    scene.quote(0, 100.0, 0.5); // asset 1 has no book and no trade
    static_cast<void>(act(agent, scene));

    scene.quote(1, 50.0, 0.5); // its first price: only starts the average
    set_prices(scene, {100.0, 50.0});
    CHECK(act(agent, scene).empty());

    set_prices(scene, {100.0, 60.0}); // now it has an average to compare with
    const auto actions = act(agent, scene);
    REQUIRE(actions.size() == 1);
    CHECK(std::get<agents::SubmitMarket>(actions[0]).asset == 1);
}

TEST_CASE("The price falls back on the last trade when a side of the book is empty",
          "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(), Stance::Follow);
    Scene scene(1);
    scene.markets[0].trades = {
        agents::TradePrint{.price = 100.0, .quantity = 1, .aggressor = Side::Buy}};
    static_cast<void>(act(agent, scene));

    scene.markets[0].trades = {
        agents::TradePrint{.price = 104.0, .quantity = 1, .aggressor = Side::Buy}};

    CHECK(only(act(agent, scene)).side == Side::Buy);
}

TEST_CASE("The number of assets cannot change, and every asset needs a position",
          "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(), Stance::Follow);
    Scene two = scene_around({100.0, 50.0});
    static_cast<void>(act(agent, two));

    Scene three = scene_around({100.0, 50.0, 20.0});
    CHECK_THROWS_AS(act(agent, three), std::invalid_argument);

    agents::DeviationTraderAgent other(params(), Stance::Follow);
    Scene no_position = scene_around({100.0});
    no_position.positions.clear();
    CHECK_THROWS_AS(act(other, no_position), std::invalid_argument);
}

TEST_CASE("It draws no random number", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(1.0, 0.0), Stance::Follow);
    Scene scene = scene_around({100.0});
    rng::RngEngine eng = engine();
    const rng::RngEngine before = eng;
    std::vector<agents::Action> out;
    agent.act(scene.view(), eng, out);
    set_prices(scene, {110.0});

    agent.act(scene.view(), eng, out);

    CHECK(eng == before);
    CHECK(out.size() == 1);
}

TEST_CASE("act() appends to the buffer and does not clear it", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(agent, scene));
    set_prices(scene, {110.0});
    rng::RngEngine eng = engine();
    std::vector<agents::Action> out;
    out.emplace_back(agents::Cancel{.asset = 9, .id = mm::OrderId{9}});

    agent.act(scene.view(), eng, out);

    CHECK(out.size() == 2);
    CHECK(std::holds_alternative<agents::Cancel>(out[0]));
}

// ------------------------------------------------------------------------- state

TEST_CASE("Nothing is saved before the first act", "[agents][deviation]") {
    const agents::DeviationTraderAgent agent(params(), Stance::Follow);

    CHECK(agent.save().empty());
}

TEST_CASE("An agent restored from a saved state carries on exactly like the original",
          "[agents][deviation]") {
    agents::DeviationTraderAgent original(params(2.0, 0.02), Stance::Follow);
    Scene scene = scene_around({100.0, 50.0});
    static_cast<void>(act(original, scene));
    set_prices(scene, {103.0, 49.0});
    static_cast<void>(act(original, scene));

    agents::DeviationTraderAgent restored(params(2.0, 0.02), Stance::Follow);
    restored.load(original.save());

    set_prices(scene, {108.0, 51.0});
    const auto from_original = act(original, scene);
    const auto from_restored = act(restored, scene);

    REQUIRE(from_original.size() == from_restored.size());
    REQUIRE_FALSE(from_original.empty());
    for (std::size_t i = 0; i < from_original.size(); ++i) {
        const auto& a = std::get<agents::SubmitMarket>(from_original[i]);
        const auto& b = std::get<agents::SubmitMarket>(from_restored[i]);
        CHECK(a.asset == b.asset);
        CHECK(a.side == b.side);
        CHECK(a.quantity == b.quantity);
    }
}

TEST_CASE("The remembered average matters: a fresh agent has nothing to compare with",
          "[agents][deviation]") {
    agents::DeviationTraderAgent informed(params(), Stance::Follow);
    agents::DeviationTraderAgent fresh(params(), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(informed, scene));
    set_prices(scene, {110.0});

    CHECK(act(informed, scene).size() == 1);
    CHECK(act(fresh, scene).empty()); // its first price only starts its average
}

TEST_CASE("An empty state forgets the averages", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(), Stance::Follow);
    Scene scene = scene_around({100.0});
    static_cast<void>(act(agent, scene));
    REQUIRE_FALSE(agent.save().empty());

    agent.load({});

    CHECK(agent.save().empty());
}

TEST_CASE("A saved state that is not one is refused and changes nothing", "[agents][deviation]") {
    agents::DeviationTraderAgent agent(params(), Stance::Follow);
    Scene scene = scene_around({100.0, 50.0});
    static_cast<void>(act(agent, scene));
    const std::vector<std::byte> good = agent.save();

    CHECK_THROWS_AS(agent.load(std::vector<std::byte>(3, std::byte{1})), std::invalid_argument);
    CHECK_THROWS_AS(agent.load(std::vector<std::byte>(sizeof(std::uint64_t), std::byte{0})),
                    std::invalid_argument);
    CHECK(agent.save() == good);
}
