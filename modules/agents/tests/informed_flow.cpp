/// \file
/// Tests for agents/informed_flow.hpp. The informed trader is driven with hand-built views.
/// What matters: the orders are immediate market orders of the right size and in the right
/// number; they follow the last move of the signal as often as the spec says; the draws are
/// exactly those of agents/flow.hpp in their documented order; a rejected act draws nothing; and
/// what the agent remembers of the last signal is saved, restored, and makes a difference.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <variant>
#include <vector>

#include <agents/flow.hpp>
#include <agents/informed_flow.hpp>
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

agents::InformedFlowSpec spec(double lambda = 2.0,
                              double correlation = 0.5,
                              agents::DistributionSpec size = agents::UniformIntSpec{3, 7}) {
    return agents::InformedFlowSpec{
        .lambda = lambda, .direction_correlation = correlation, .size = size};
}

// A scene whose assets all have a signal.
Scene scene_at(std::vector<double> signals) {
    Scene scene(signals.size());
    for (std::size_t i = 0; i < signals.size(); ++i) {
        scene.markets[i].signal = signals[i];
    }
    return scene;
}

void act(agents::InformedFlowAgent& agent,
         Scene& scene,
         rng::RngEngine& rng,
         std::vector<agents::Action>& out) {
    agent.act(scene.view(), rng, out);
}

std::size_t count_side(const std::vector<agents::Action>& actions, Side side) {
    std::size_t count = 0;
    for (const agents::Action& action : actions) {
        count += std::get<agents::SubmitMarket>(action).side == side ? 1U : 0U;
    }
    return count;
}

} // namespace

// ------------------------------------------------------------------ construction

TEST_CASE("The informed trader refuses a spec that makes no sense", "[agents][informed_flow]") {
    for (const double lambda : {-1.0, quiet_nan, infinity}) {
        CHECK_THROWS_AS(agents::InformedFlowAgent(spec(lambda)), std::invalid_argument);
    }
    for (const double correlation : {-0.1, 1.1, quiet_nan, infinity}) {
        CHECK_THROWS_AS(agents::InformedFlowAgent(spec(1.0, correlation)), std::invalid_argument);
    }
    CHECK_THROWS_AS(agents::InformedFlowAgent(spec(1.0, 0.5, agents::UniformIntSpec{0, 5})),
                    std::invalid_argument);
    CHECK_THROWS_AS(agents::InformedFlowAgent(spec(1.0, 0.5, agents::UniformIntSpec{5, 1})),
                    std::invalid_argument);
    CHECK_THROWS_AS(agents::InformedFlowAgent(spec(1.0, 0.5, agents::ExponentialSpec{0.0})),
                    std::invalid_argument);

    CHECK_NOTHROW(agents::InformedFlowAgent(spec(0.0, 0.0)));
    CHECK_NOTHROW(agents::InformedFlowAgent(spec(1.0, 1.0)));
}

// --------------------------------------------------------------------- rejections

TEST_CASE("A view it cannot use is refused before anything is drawn", "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec());
    rng::RngEngine eng = engine();
    const rng::RngEngine before = eng;
    std::vector<agents::Action> out;

    Scene no_signal = scene_at({100.0, 50.0});
    no_signal.markets[1].signal = std::nullopt;
    CHECK_THROWS_AS(act(agent, no_signal, eng, out), std::invalid_argument);

    Scene not_finite = scene_at({100.0, 50.0});
    not_finite.markets[0].signal = quiet_nan;
    CHECK_THROWS_AS(act(agent, not_finite, eng, out), std::invalid_argument);

    for (const double elapsed : {0.0, -1.0, quiet_nan, infinity}) {
        Scene scene = scene_at({100.0});
        scene.elapsed = elapsed;
        CHECK_THROWS_AS(act(agent, scene, eng, out), std::invalid_argument);
    }

    agents::InformedFlowAgent hasty(spec(1e5));
    Scene long_wait = scene_at({100.0});
    long_wait.elapsed = 100.0; // 1e7 arrivals
    CHECK_THROWS_AS(act(hasty, long_wait, eng, out), std::invalid_argument);

    CHECK(eng == before);
    CHECK(out.empty());
}

TEST_CASE("The number of assets cannot change between two acts", "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec());
    rng::RngEngine eng = engine();
    std::vector<agents::Action> out;
    Scene two = scene_at({100.0, 50.0});
    act(agent, two, eng, out);

    const rng::RngEngine before = eng;
    Scene three = scene_at({100.0, 50.0, 20.0});

    CHECK_THROWS_AS(act(agent, three, eng, out), std::invalid_argument);
    CHECK(eng == before);
}

// -------------------------------------------------------------------------- orders

TEST_CASE("With a rate of zero it sends nothing and draws nothing, but remembers the signal",
          "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec(0.0));
    rng::RngEngine eng = engine();
    const rng::RngEngine before = eng;
    std::vector<agents::Action> out;
    Scene scene = scene_at({100.0});

    act(agent, scene, eng, out);

    CHECK(out.empty());
    CHECK(eng == before);
    CHECK_FALSE(agent.save().empty());
}

TEST_CASE("Every order is an immediate market order of a size the spec allows",
          "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec(5.0, 0.5, agents::UniformIntSpec{3, 7}));
    rng::RngEngine eng = engine(3);
    std::vector<agents::Action> out;
    Scene scene = scene_at({100.0, 50.0});

    for (int i = 0; i < 20; ++i) {
        act(agent, scene, eng, out);
    }

    REQUIRE_FALSE(out.empty());
    for (const agents::Action& action : out) {
        const auto* order = std::get_if<agents::SubmitMarket>(&action);
        REQUIRE(order != nullptr);
        CHECK(order->asset < 2);
        CHECK(order->time_in_force == mm::MarketTimeInForce::IOC);
        CHECK(order->quantity >= 3);
        CHECK(order->quantity <= 7);
    }
}

TEST_CASE("The number of orders averages lambda times the time elapsed, on each asset",
          "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec(2.0));
    rng::RngEngine eng = engine(5);
    std::vector<agents::Action> out;
    Scene scene = scene_at({100.0, 50.0});
    scene.elapsed = 1.0;

    for (int i = 0; i < 500; ++i) {
        act(agent, scene, eng, out);
    }

    // 500 acts x 2 assets x 2 per unit of time = 2000, a standard deviation of about 45.
    const auto total = static_cast<double>(out.size());
    CHECK(std::abs(total - 2000.0) < 180.0);
}

TEST_CASE("A longer wait brings proportionally more orders", "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec(1.0));
    rng::RngEngine eng = engine(6);
    std::vector<agents::Action> out;
    Scene scene = scene_at({100.0});
    scene.elapsed = 10.0;

    for (int i = 0; i < 200; ++i) {
        act(agent, scene, eng, out);
    }

    // 200 acts x 10 = 2000 on average.
    CHECK(std::abs(static_cast<double>(out.size()) - 2000.0) < 180.0);
}

TEST_CASE("The first act has no move to follow, so even a full correlation is a coin",
          "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec(400.0, 1.0));
    rng::RngEngine eng = engine(8);
    std::vector<agents::Action> out;
    Scene scene = scene_at({100.0});

    act(agent, scene, eng, out);

    const double buys = static_cast<double>(count_side(out, Side::Buy));
    const double all = static_cast<double>(out.size());
    REQUIRE(all > 300.0);
    CHECK(buys / all > 0.4);
    CHECK(buys / all < 0.6);
}

TEST_CASE("After a rise it buys, after a fall it sells, with a full correlation",
          "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec(50.0, 1.0));
    rng::RngEngine eng = engine(9);
    std::vector<agents::Action> ignored;
    Scene scene = scene_at({100.0});
    act(agent, scene, eng, ignored); // the first signal

    scene.markets[0].signal = 101.0;
    std::vector<agents::Action> after_rise;
    act(agent, scene, eng, after_rise);

    scene.markets[0].signal = 99.5;
    std::vector<agents::Action> after_fall;
    act(agent, scene, eng, after_fall);

    REQUIRE(after_rise.size() > 10);
    REQUIRE(after_fall.size() > 10);
    CHECK(count_side(after_rise, Side::Sell) == 0);
    CHECK(count_side(after_fall, Side::Buy) == 0);
}

TEST_CASE("A signal that has not moved is a coin", "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec(400.0, 1.0));
    rng::RngEngine eng = engine(10);
    std::vector<agents::Action> ignored;
    Scene scene = scene_at({100.0});
    act(agent, scene, eng, ignored);

    std::vector<agents::Action> out;
    act(agent, scene, eng, out); // the same signal again

    const double buys = static_cast<double>(count_side(out, Side::Buy));
    CHECK(buys / static_cast<double>(out.size()) > 0.4);
    CHECK(buys / static_cast<double>(out.size()) < 0.6);
}

TEST_CASE("Each asset follows its own move", "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec(50.0, 1.0));
    rng::RngEngine eng = engine(11);
    std::vector<agents::Action> ignored;
    Scene scene = scene_at({100.0, 100.0});
    act(agent, scene, eng, ignored);

    scene.markets[0].signal = 102.0; // asset 0 rose
    scene.markets[1].signal = 98.0;  // asset 1 fell
    std::vector<agents::Action> out;
    act(agent, scene, eng, out);

    std::size_t on_rising = 0;
    std::size_t on_falling = 0;
    for (const agents::Action& action : out) {
        const auto& order = std::get<agents::SubmitMarket>(action);
        if (order.asset == 0) {
            CHECK(order.side == Side::Buy);
            ++on_rising;
        } else {
            CHECK(order.side == Side::Sell);
            ++on_falling;
        }
    }
    CHECK(on_rising > 10);
    CHECK(on_falling > 10);
}

TEST_CASE("The orders are exactly the documented draws, in the documented order",
          "[agents][informed_flow]") {
    const agents::InformedFlowSpec s = spec(4.0, 0.6, agents::ExponentialSpec{.mean = 4.0});
    agents::InformedFlowAgent agent(s);
    rng::RngEngine eng = engine(12);
    Scene scene = scene_at({100.0, 50.0});
    std::vector<agents::Action> first;
    act(agent, scene, eng, first);
    scene.markets[0].signal = 101.0;
    scene.markets[1].signal = 49.0;
    scene.elapsed = 2.0;

    rng::RngEngine reference = eng;
    std::vector<agents::Action> out;
    act(agent, scene, eng, out);

    // The same draws by hand: asset by asset, the count, then each order's direction and size.
    std::vector<agents::SubmitMarket> expected;
    const double moves[2] = {1.0, -1.0};
    for (std::size_t asset = 0; asset < 2; ++asset) {
        const std::int64_t count = agents::draw_arrival_count(s.lambda, 2.0, reference);
        for (std::int64_t i = 0; i < count; ++i) {
            const Side side =
                agents::draw_direction(s.direction_correlation, moves[asset], reference);
            const mm::Quantity quantity = agents::draw_size(s.size, reference);
            expected.push_back(
                agents::SubmitMarket{.asset = asset, .side = side, .quantity = quantity});
        }
    }
    REQUIRE(out.size() == expected.size());
    REQUIRE(out.size() > 3);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto& order = std::get<agents::SubmitMarket>(out[i]);
        CHECK(order.asset == expected[i].asset);
        CHECK(order.side == expected[i].side);
        CHECK(order.quantity == expected[i].quantity);
    }
    CHECK(eng == reference); // and not one word more or less
}

TEST_CASE("act() appends to the buffer and does not clear it", "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec(0.0));
    rng::RngEngine eng = engine();
    std::vector<agents::Action> out;
    out.emplace_back(agents::Cancel{.asset = 9, .id = mm::OrderId{9}});
    Scene scene = scene_at({100.0});

    act(agent, scene, eng, out);

    CHECK(out.size() == 1);
}

// ------------------------------------------------------------------------- state

TEST_CASE("Nothing is saved before the first act", "[agents][informed_flow]") {
    const agents::InformedFlowAgent agent(spec());

    CHECK(agent.save().empty());
}

TEST_CASE("The saved state is the count and the last signal of each asset",
          "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec(0.0));
    rng::RngEngine eng = engine();
    std::vector<agents::Action> out;
    Scene scene = scene_at({100.0, 50.0, 25.0});
    act(agent, scene, eng, out);

    const std::vector<std::byte> state = agent.save();

    CHECK(state.size() == sizeof(std::uint64_t) + (3 * sizeof(double)));
    CHECK(agent.save() == state); // saving changes nothing
}

TEST_CASE("An agent restored from a saved state carries on exactly like the original",
          "[agents][informed_flow]") {
    const agents::InformedFlowSpec s = spec(30.0, 1.0);
    agents::InformedFlowAgent original(s);
    rng::RngEngine eng = engine(14);
    std::vector<agents::Action> ignored;
    Scene scene = scene_at({100.0, 50.0});
    act(original, scene, eng, ignored);
    scene.markets[0].signal = 103.0;
    scene.markets[1].signal = 47.0;
    act(original, scene, eng, ignored);

    agents::InformedFlowAgent restored(s);
    restored.load(original.save());

    scene.markets[0].signal = 104.0;
    scene.markets[1].signal = 48.0;
    rng::RngEngine eng_a = eng;
    rng::RngEngine eng_b = eng;
    std::vector<agents::Action> from_original;
    std::vector<agents::Action> from_restored;
    act(original, scene, eng_a, from_original);
    act(restored, scene, eng_b, from_restored);

    REQUIRE(from_original.size() == from_restored.size());
    REQUIRE(from_original.size() > 10);
    for (std::size_t i = 0; i < from_original.size(); ++i) {
        const auto& a = std::get<agents::SubmitMarket>(from_original[i]);
        const auto& b = std::get<agents::SubmitMarket>(from_restored[i]);
        CHECK(a.asset == b.asset);
        CHECK(a.side == b.side);
        CHECK(a.quantity == b.quantity);
    }
}

TEST_CASE("The remembered signal matters: a fresh agent does not see the move",
          "[agents][informed_flow]") {
    const agents::InformedFlowSpec s = spec(60.0, 1.0);
    agents::InformedFlowAgent informed(s);
    agents::InformedFlowAgent fresh(s);
    rng::RngEngine eng = engine(15);
    std::vector<agents::Action> ignored;
    Scene scene = scene_at({100.0});
    act(informed, scene, eng, ignored);
    scene.markets[0].signal = 105.0;

    rng::RngEngine eng_a = eng;
    rng::RngEngine eng_b = eng;
    std::vector<agents::Action> sees_the_move;
    std::vector<agents::Action> sees_nothing;
    act(informed, scene, eng_a, sees_the_move);
    act(fresh, scene, eng_b, sees_nothing);

    CHECK(count_side(sees_the_move, Side::Sell) == 0);
    CHECK(count_side(sees_nothing, Side::Sell) > 5); // no move to follow, so a coin
}

TEST_CASE("An empty state forgets the signals", "[agents][informed_flow]") {
    const agents::InformedFlowSpec s = spec(0.0);
    agents::InformedFlowAgent agent(s);
    rng::RngEngine eng = engine();
    std::vector<agents::Action> out;
    Scene scene = scene_at({100.0});
    act(agent, scene, eng, out);
    REQUIRE_FALSE(agent.save().empty());

    agent.load({});

    CHECK(agent.save().empty());
}

TEST_CASE("A saved state that is not one is refused and changes nothing",
          "[agents][informed_flow]") {
    agents::InformedFlowAgent agent(spec(0.0));
    rng::RngEngine eng = engine();
    std::vector<agents::Action> out;
    Scene scene = scene_at({100.0, 50.0});
    act(agent, scene, eng, out);
    const std::vector<std::byte> good = agent.save();

    const std::vector<std::byte> too_short(3, std::byte{1});
    std::vector<std::byte> truncated = good;
    truncated.pop_back();
    std::vector<std::byte> one_too_many = good;
    one_too_many.insert(one_too_many.end(), sizeof(double), std::byte{0});
    std::vector<std::byte> zero_count(sizeof(std::uint64_t), std::byte{0});
    zero_count.insert(zero_count.end(), sizeof(double), std::byte{0});
    std::vector<std::byte> huge_count(sizeof(std::uint64_t), std::byte{0xFF});
    huge_count.insert(huge_count.end(), sizeof(double), std::byte{0});

    // A count of zero with nothing after it is not something save() ever writes either.
    const std::vector<std::byte> bare_zero(sizeof(std::uint64_t), std::byte{0});
    // A whole state followed by a few bytes that do not make a signal.
    std::vector<std::byte> ragged = good;
    ragged.insert(ragged.end(), 3, std::byte{0});

    CHECK_THROWS_AS(agent.load(bare_zero), std::invalid_argument);
    CHECK_THROWS_AS(agent.load(ragged), std::invalid_argument);
    CHECK_THROWS_AS(agent.load(too_short), std::invalid_argument);
    CHECK_THROWS_AS(agent.load(truncated), std::invalid_argument);
    CHECK_THROWS_AS(agent.load(one_too_many), std::invalid_argument);
    CHECK_THROWS_AS(agent.load(zero_count), std::invalid_argument);
    CHECK_THROWS_AS(agent.load(huge_count), std::invalid_argument);
    CHECK(agent.save() == good);
}
