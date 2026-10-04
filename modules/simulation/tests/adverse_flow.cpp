/// \file
/// Tests for simulation/adverse_flow.hpp. The statistical checks run on a
/// fixed seed, so they are deterministic (they pass or fail the same way
/// every run) and use tolerances of several standard errors. What matters
/// more than the statistics: the exact number of engine words each draw
/// consumes (the documented fixed draw order is what makes a run
/// reproducible), that a rejected call leaves the engine untouched, and that
/// generate_adverse_flow is exactly the documented composition of the three
/// draws.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/types.hpp>

#include <math/dist/exponential.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/seed.hpp>
#include <math/rng/stream.hpp>
#include <simulation/adverse_flow.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/size_spec.hpp>

namespace sim = mms::simulation;
namespace math = mms::math;
namespace mm = mms::mm;

namespace {

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

math::rng::RngEngine fresh_engine(std::uint64_t seed = 1) {
    const math::rng::RngHub hub{math::rng::Seed{seed}};
    return hub.engine_for(
        {.subsystem = math::rng::Subsystem::AdverseFlow, .asset = math::rng::AssetKey{0}});
}

/// True if `engine` has not consumed a single word since `reference` was copied.
bool untouched(math::rng::RngEngine engine, math::rng::RngEngine reference) {
    return engine() == reference();
}

/// True if `engine` sits exactly `words` words past `reference`.
bool advanced_by(math::rng::RngEngine engine, math::rng::RngEngine reference, int words) {
    for (int i = 0; i < words; ++i) {
        static_cast<void>(reference());
    }
    return engine() == reference();
}

sim::AssetSpec flow_asset(double lambda, double correlation, sim::DistributionSpec size) {
    return sim::AssetSpec{.symbol = "FLOW",
                          .initial_mid = 100.0,
                          .price_volatility = 0.2,
                          .factor_model = std::nullopt,
                          .adverse_flow_lambda = lambda,
                          .adverse_flow_direction_correlation = correlation,
                          .adverse_flow_size = size};
}

struct Scenario {
    double correlation;
    double last_move;
};

double buy_fraction(Scenario scenario, int draws) {
    math::rng::RngEngine engine = fresh_engine(7);
    int buys = 0;
    for (int i = 0; i < draws; ++i) {
        if (sim::draw_direction(scenario.correlation, scenario.last_move, engine) ==
            mm::Side::Buy) {
            ++buys;
        }
    }
    return static_cast<double>(buys) / draws;
}

} // namespace

// ---------------------------------------------------------------- arrivals

TEST_CASE("A zero rate gives no arrivals and consumes nothing", "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;

    CHECK(sim::draw_arrival_count(0.0, 1.0, engine) == 0);
    CHECK(untouched(engine, before));
}

TEST_CASE("The arrival count averages lambda * dt", "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine(11);
    constexpr int ticks = 20000;
    std::int64_t total = 0;

    for (int i = 0; i < ticks; ++i) {
        total += sim::draw_arrival_count(5.0, 0.5, engine); // mean 2.5
    }

    CHECK(static_cast<double>(total) / ticks == Catch::Approx(2.5).margin(0.06));
}

TEST_CASE("A mean above Poisson's limit of 30 is still exact in the mean",
          "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine(13);
    constexpr int ticks = 2000;
    std::int64_t total = 0;

    for (int i = 0; i < ticks; ++i) {
        total += sim::draw_arrival_count(100.0, 1.0, engine); // split into chunks of at most 30
    }

    CHECK(static_cast<double>(total) / ticks == Catch::Approx(100.0).margin(1.2));
}

TEST_CASE("The same engine state gives the same arrival count", "[simulation][adverse_flow]") {
    math::rng::RngEngine first = fresh_engine(5);
    math::rng::RngEngine second = fresh_engine(5);

    for (int i = 0; i < 50; ++i) {
        CHECK(sim::draw_arrival_count(3.0, 1.0, first) ==
              sim::draw_arrival_count(3.0, 1.0, second));
    }
}

TEST_CASE("draw_arrival_count rejects invalid inputs and consumes nothing",
          "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;

    for (const double bad : {-1.0, quiet_nan, infinity}) {
        CHECK_THROWS_AS(sim::draw_arrival_count(bad, 1.0, engine), std::invalid_argument);
    }
    for (const double bad : {0.0, -1.0, quiet_nan, infinity}) {
        CHECK_THROWS_AS(sim::draw_arrival_count(1.0, bad, engine), std::invalid_argument);
    }
    CHECK_THROWS_AS(sim::draw_arrival_count(2.0e6, 1.0, engine), std::invalid_argument);
    CHECK_THROWS_AS(sim::draw_arrival_count(1.0e300, 1.0e300, engine), std::invalid_argument);
    CHECK(untouched(engine, before));
}

// --------------------------------------------------------------- direction

TEST_CASE("Full correlation always follows the sign of the last move",
          "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();

    for (int i = 0; i < 200; ++i) {
        CHECK(sim::draw_direction(1.0, 0.3, engine) == mm::Side::Buy);
        CHECK(sim::draw_direction(1.0, -0.3, engine) == mm::Side::Sell);
    }
}

TEST_CASE("Zero correlation is a fair coin whatever the move", "[simulation][adverse_flow]") {
    CHECK(buy_fraction({.correlation = 0.0, .last_move = 5.0}, 20000) ==
          Catch::Approx(0.5).margin(0.025));
    CHECK(buy_fraction({.correlation = 0.0, .last_move = -5.0}, 20000) ==
          Catch::Approx(0.5).margin(0.025));
}

TEST_CASE("After an up move P(Buy) is (1 + correlation) / 2", "[simulation][adverse_flow]") {
    CHECK(buy_fraction({.correlation = 0.5, .last_move = 1.0}, 20000) ==
          Catch::Approx(0.75).margin(0.025));
    CHECK(buy_fraction({.correlation = 0.2, .last_move = 1.0}, 20000) ==
          Catch::Approx(0.6).margin(0.025));
}

TEST_CASE("After a down move P(Sell) is (1 + correlation) / 2", "[simulation][adverse_flow]") {
    CHECK(1.0 - buy_fraction({.correlation = 0.5, .last_move = -1.0}, 20000) ==
          Catch::Approx(0.75).margin(0.025));
}

TEST_CASE("A move of exactly zero has no direction to follow, so it is a fair coin",
          "[simulation][adverse_flow]") {
    CHECK(buy_fraction({.correlation = 1.0, .last_move = 0.0}, 20000) ==
          Catch::Approx(0.5).margin(0.025));
}

TEST_CASE("Every direction draw consumes exactly two engine words", "[simulation][adverse_flow]") {
    // Both coins are always drawn, so the stream never depends on the outcome:
    // a sweep of the correlation must not desynchronise anything downstream.
    for (const double correlation : {0.0, 0.3, 1.0}) {
        for (const double move : {-2.0, 0.0, 2.0}) {
            math::rng::RngEngine engine = fresh_engine(3);
            const math::rng::RngEngine before = engine;

            static_cast<void>(sim::draw_direction(correlation, move, engine));

            CHECK(advanced_by(engine, before, 2));
        }
    }
}

TEST_CASE("draw_direction rejects invalid inputs and consumes nothing",
          "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;

    for (const double bad : {-0.1, 1.1, quiet_nan, infinity}) {
        CHECK_THROWS_AS(sim::draw_direction(bad, 1.0, engine), std::invalid_argument);
    }
    for (const double bad : {quiet_nan, infinity, -infinity}) {
        CHECK_THROWS_AS(sim::draw_direction(0.5, bad, engine), std::invalid_argument);
    }
    CHECK(untouched(engine, before));
}

// -------------------------------------------------------------------- size

TEST_CASE("A uniform size stays in its range and reaches every value",
          "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine(17);
    std::set<mm::Quantity> seen;

    for (int i = 0; i < 2000; ++i) {
        const mm::Quantity size = sim::draw_size(sim::UniformIntSpec{.min = 3, .max = 7}, engine);
        CHECK(size >= 3);
        CHECK(size <= 7);
        seen.insert(size);
    }
    CHECK(seen == std::set<mm::Quantity>{3, 4, 5, 6, 7});
}

TEST_CASE("A uniform size with min == max is that size", "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();

    CHECK(sim::draw_size(sim::UniformIntSpec{.min = 9, .max = 9}, engine) == 9);
}

TEST_CASE("An exponential size is at least 1 and averages about its mean",
          "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine(19);
    constexpr int draws = 5000;
    double total = 0.0;

    for (int i = 0; i < draws; ++i) {
        const mm::Quantity size = sim::draw_size(sim::ExponentialSpec{.mean = 10.0}, engine);
        CHECK(size >= 1);
        total += static_cast<double>(size);
    }
    // Rounding and the floor at 1 bias the mean very slightly upward.
    CHECK(total / draws == Catch::Approx(10.0).margin(0.8));
}

TEST_CASE("An exponential size is the raw exponential draw rounded to the nearest share",
          "[simulation][adverse_flow]") {
    // Deterministic, unlike the statistical test above: replay the same
    // stream, take the raw exponential draw by hand, and round it. Rounding
    // down instead of to nearest differs on about half of all draws.
    math::rng::RngEngine engine = fresh_engine(29);
    math::rng::RngEngine replay = engine;

    for (int i = 0; i < 2000; ++i) {
        const double raw = math::dist::exponential(replay, 1.0 / 10.0);
        const auto expected = static_cast<mm::Quantity>(std::max(1.0, std::round(raw)));

        CHECK(sim::draw_size(sim::ExponentialSpec{.mean = 10.0}, engine) == expected);
    }
}

TEST_CASE("A tiny exponential mean still gives a trade of one share",
          "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();

    for (int i = 0; i < 200; ++i) {
        CHECK(sim::draw_size(sim::ExponentialSpec{.mean = 1.0e-3}, engine) == 1);
    }
}

TEST_CASE("A huge exponential mean is clamped, not overflowed", "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();

    const mm::Quantity size = sim::draw_size(sim::ExponentialSpec{.mean = 1.0e300}, engine);

    CHECK(size == 9'000'000'000'000'000);
}

TEST_CASE("draw_size rejects invalid specs and consumes nothing", "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;

    CHECK_THROWS_AS(sim::draw_size(sim::UniformIntSpec{.min = 0, .max = 5}, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(sim::draw_size(sim::UniformIntSpec{.min = -3, .max = 5}, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(sim::draw_size(sim::UniformIntSpec{.min = 6, .max = 5}, engine),
                    std::invalid_argument);
    for (const double bad : {0.0, -1.0, quiet_nan, infinity}) {
        CHECK_THROWS_AS(sim::draw_size(sim::ExponentialSpec{.mean = bad}, engine),
                        std::invalid_argument);
    }
    CHECK(untouched(engine, before));
}

// ---------------------------------------------------------- whole-tick flow

TEST_CASE("A zero rate generates no orders", "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();

    const auto orders = sim::generate_adverse_flow(
        flow_asset(0.0, 0.5, sim::UniformIntSpec{.min = 1, .max = 5}), 1.0, 1.0, engine);

    CHECK(orders.empty());
}

TEST_CASE("Every generated order is a Market IOC order of at least one share",
          "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine(23);
    int generated = 0;

    for (int tick = 0; tick < 200; ++tick) {
        const auto orders = sim::generate_adverse_flow(
            flow_asset(4.0, 0.5, sim::ExponentialSpec{.mean = 6.0}), 1.0, 1.0, engine);
        for (const mm::MarketOrder& order : orders) {
            CHECK(order.time_in_force == mm::MarketTimeInForce::IOC);
            CHECK(order.quantity >= 1);
            ++generated;
        }
    }
    CHECK(generated > 0);
}

TEST_CASE("The number of orders is the arrival count of the same engine state",
          "[simulation][adverse_flow]") {
    const sim::AssetSpec asset = flow_asset(6.0, 0.3, sim::UniformIntSpec{.min = 1, .max = 9});

    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        math::rng::RngEngine engine = fresh_engine(seed);
        math::rng::RngEngine replay = engine;

        const auto orders = sim::generate_adverse_flow(asset, 1.0, 1.0, engine);

        CHECK(static_cast<std::int64_t>(orders.size()) ==
              sim::draw_arrival_count(6.0, 1.0, replay));
    }
}

TEST_CASE("generate_adverse_flow is exactly the documented composition of the draws",
          "[simulation][adverse_flow]") {
    // Arrival count first, then for each occurrence its direction, then its
    // size. Replaying that order by hand must reproduce every order.
    const sim::AssetSpec asset = flow_asset(8.0, 0.4, sim::UniformIntSpec{.min = 1, .max = 50});
    constexpr double last_move = -0.7;

    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        math::rng::RngEngine engine = fresh_engine(seed);
        math::rng::RngEngine replay = engine;

        const auto orders = sim::generate_adverse_flow(asset, last_move, 1.0, engine);

        const std::int64_t count = sim::draw_arrival_count(8.0, 1.0, replay);
        REQUIRE(std::cmp_equal(orders.size(), count));
        for (const mm::MarketOrder& order : orders) {
            const mm::Side side = sim::draw_direction(0.4, last_move, replay);
            const mm::Quantity quantity = sim::draw_size(asset.adverse_flow_size, replay);
            CHECK(order.side == side);
            CHECK(order.quantity == quantity);
        }
        CHECK(untouched(engine, replay)); // and not one word more or less
    }
}

TEST_CASE("The same engine state gives the same orders", "[simulation][adverse_flow]") {
    const sim::AssetSpec asset = flow_asset(5.0, 0.6, sim::ExponentialSpec{.mean = 4.0});
    math::rng::RngEngine first = fresh_engine(31);
    math::rng::RngEngine second = fresh_engine(31);

    for (int tick = 0; tick < 50; ++tick) {
        const auto a = sim::generate_adverse_flow(asset, 0.2, 1.0, first);
        const auto b = sim::generate_adverse_flow(asset, 0.2, 1.0, second);
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK(a[i].side == b[i].side);
            CHECK(a[i].quantity == b[i].quantity);
        }
    }
}

TEST_CASE("With full correlation every order follows the last move", "[simulation][adverse_flow]") {
    const sim::AssetSpec asset = flow_asset(5.0, 1.0, sim::UniformIntSpec{.min = 1, .max = 5});
    math::rng::RngEngine engine = fresh_engine(37);
    int checked = 0;

    for (int tick = 0; tick < 100; ++tick) {
        for (const mm::MarketOrder& order : sim::generate_adverse_flow(asset, 2.0, 1.0, engine)) {
            CHECK(order.side == mm::Side::Buy);
            ++checked;
        }
        for (const mm::MarketOrder& order : sim::generate_adverse_flow(asset, -2.0, 1.0, engine)) {
            CHECK(order.side == mm::Side::Sell);
            ++checked;
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("The asset's own size spec is the one used", "[simulation][adverse_flow]") {
    const sim::AssetSpec asset = flow_asset(6.0, 0.0, sim::UniformIntSpec{.min = 4, .max = 4});
    math::rng::RngEngine engine = fresh_engine(41);
    int checked = 0;

    for (int tick = 0; tick < 100; ++tick) {
        for (const mm::MarketOrder& order : sim::generate_adverse_flow(asset, 0.0, 1.0, engine)) {
            CHECK(order.quantity == 4);
            ++checked;
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("generate_adverse_flow validates everything up front, even at a zero rate",
          "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;
    const sim::DistributionSpec fine = sim::UniformIntSpec{.min = 1, .max = 5};

    // Zero rate: the loop would never run, so these only fail if checked first.
    CHECK_THROWS_AS(sim::generate_adverse_flow(flow_asset(0.0, 1.5, fine), 1.0, 1.0, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(sim::generate_adverse_flow(flow_asset(0.0, -0.1, fine), 1.0, 1.0, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        sim::generate_adverse_flow(
            flow_asset(0.0, 0.5, sim::UniformIntSpec{.min = 0, .max = 5}), 1.0, 1.0, engine),
        std::invalid_argument);
    CHECK_THROWS_AS(sim::generate_adverse_flow(
                        flow_asset(0.0, 0.5, sim::ExponentialSpec{.mean = -2.0}), 1.0, 1.0, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(sim::generate_adverse_flow(flow_asset(0.0, 0.5, fine), quiet_nan, 1.0, engine),
                    std::invalid_argument);

    // And the rate and the time step themselves.
    CHECK_THROWS_AS(sim::generate_adverse_flow(flow_asset(-1.0, 0.5, fine), 1.0, 1.0, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(sim::generate_adverse_flow(flow_asset(1.0, 0.5, fine), 1.0, 0.0, engine),
                    std::invalid_argument);

    CHECK(untouched(engine, before)); // not one of those rejections moved the stream
}
