/// \file
/// Tests for agents/flow.hpp, the random draws of an agent that trades at random. The
/// statistical checks run on a fixed seed, so they are deterministic (they pass or fail the
/// same way every run) and use tolerances of several standard errors. What matters more than
/// the statistics: the exact number of engine words each draw consumes (the documented fixed
/// draw order is what makes a run reproducible), and that a rejected call leaves the engine
/// untouched.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <agents/flow.hpp>
#include <agents/size_spec.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/types.hpp>

#include <math/dist/exponential.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/seed.hpp>
#include <math/rng/stream.hpp>

namespace agents = mms::agents;
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

struct Scenario {
    double correlation;
    double last_move;
};

double buy_fraction(Scenario scenario, int draws) {
    math::rng::RngEngine engine = fresh_engine(7);
    int buys = 0;
    for (int i = 0; i < draws; ++i) {
        if (agents::draw_direction(scenario.correlation, scenario.last_move, engine) ==
            mm::Side::Buy) {
            ++buys;
        }
    }
    return static_cast<double>(buys) / draws;
}

} // namespace

// ---------------------------------------------------------------- arrivals

TEST_CASE("A zero rate gives no arrivals and consumes nothing", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;

    CHECK(agents::draw_arrival_count(0.0, 1.0, engine) == 0);
    CHECK(untouched(engine, before));
}

TEST_CASE("The arrival count averages lambda * dt", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine(11);
    constexpr int ticks = 20000;
    std::int64_t total = 0;

    for (int i = 0; i < ticks; ++i) {
        total += agents::draw_arrival_count(5.0, 0.5, engine); // mean 2.5
    }

    CHECK(static_cast<double>(total) / ticks == Catch::Approx(2.5).margin(0.06));
}

TEST_CASE("A mean above Poisson's limit of 30 is still exact in the mean", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine(13);
    constexpr int ticks = 2000;
    std::int64_t total = 0;

    for (int i = 0; i < ticks; ++i) {
        total += agents::draw_arrival_count(100.0, 1.0, engine); // split into chunks of at most 30
    }

    CHECK(static_cast<double>(total) / ticks == Catch::Approx(100.0).margin(1.2));
}

TEST_CASE("The same engine state gives the same arrival count", "[agents][flow]") {
    math::rng::RngEngine first = fresh_engine(5);
    math::rng::RngEngine second = fresh_engine(5);

    for (int i = 0; i < 50; ++i) {
        CHECK(agents::draw_arrival_count(3.0, 1.0, first) ==
              agents::draw_arrival_count(3.0, 1.0, second));
    }
}

TEST_CASE("draw_arrival_count rejects invalid inputs and consumes nothing", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;

    for (const double bad : {-1.0, quiet_nan, infinity}) {
        CHECK_THROWS_AS(agents::draw_arrival_count(bad, 1.0, engine), std::invalid_argument);
    }
    for (const double bad : {0.0, -1.0, quiet_nan, infinity}) {
        CHECK_THROWS_AS(agents::draw_arrival_count(1.0, bad, engine), std::invalid_argument);
    }
    CHECK_THROWS_AS(agents::draw_arrival_count(2.0e6, 1.0, engine), std::invalid_argument);
    CHECK_THROWS_AS(agents::draw_arrival_count(1.0e300, 1.0e300, engine), std::invalid_argument);
    CHECK(untouched(engine, before));
}

TEST_CASE("The most arrivals one draw accepts is exactly a million", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine();

    const std::int64_t count = agents::draw_arrival_count(1.0e6, 1.0, engine);

    // Poisson with a mean of a million: a standard deviation of a thousand.
    CHECK(std::abs(static_cast<double>(count) - 1.0e6) < 6000.0);
    CHECK_THROWS_AS(agents::draw_arrival_count(std::nextafter(1.0e6, 2.0e6), 1.0, engine),
                    std::invalid_argument);
}

// --------------------------------------------------------------- direction

TEST_CASE("Full correlation always follows the sign of the last move", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine();

    for (int i = 0; i < 200; ++i) {
        CHECK(agents::draw_direction(1.0, 0.3, engine) == mm::Side::Buy);
        CHECK(agents::draw_direction(1.0, -0.3, engine) == mm::Side::Sell);
    }
}

TEST_CASE("Zero correlation is a fair coin whatever the move", "[agents][flow]") {
    CHECK(buy_fraction({.correlation = 0.0, .last_move = 5.0}, 20000) ==
          Catch::Approx(0.5).margin(0.025));
    CHECK(buy_fraction({.correlation = 0.0, .last_move = -5.0}, 20000) ==
          Catch::Approx(0.5).margin(0.025));
}

TEST_CASE("After an up move P(Buy) is (1 + correlation) / 2", "[agents][flow]") {
    CHECK(buy_fraction({.correlation = 0.5, .last_move = 1.0}, 20000) ==
          Catch::Approx(0.75).margin(0.025));
    CHECK(buy_fraction({.correlation = 0.2, .last_move = 1.0}, 20000) ==
          Catch::Approx(0.6).margin(0.025));
}

TEST_CASE("After a down move P(Sell) is (1 + correlation) / 2", "[agents][flow]") {
    CHECK(1.0 - buy_fraction({.correlation = 0.5, .last_move = -1.0}, 20000) ==
          Catch::Approx(0.75).margin(0.025));
}

TEST_CASE("A move of exactly zero has no direction to follow, so it is a fair coin",
          "[agents][flow]") {
    CHECK(buy_fraction({.correlation = 1.0, .last_move = 0.0}, 20000) ==
          Catch::Approx(0.5).margin(0.025));
}

TEST_CASE("Every direction draw consumes exactly two engine words", "[agents][flow]") {
    // Both coins are always drawn, so the stream never depends on the outcome:
    // a sweep of the correlation must not desynchronise anything downstream.
    for (const double correlation : {0.0, 0.3, 1.0}) {
        for (const double move : {-2.0, 0.0, 2.0}) {
            math::rng::RngEngine engine = fresh_engine(3);
            const math::rng::RngEngine before = engine;

            static_cast<void>(agents::draw_direction(correlation, move, engine));

            CHECK(advanced_by(engine, before, 2));
        }
    }
}

TEST_CASE("draw_direction rejects invalid inputs and consumes nothing", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;

    for (const double bad : {-0.1, 1.1, quiet_nan, infinity}) {
        CHECK_THROWS_AS(agents::draw_direction(bad, 1.0, engine), std::invalid_argument);
    }
    for (const double bad : {quiet_nan, infinity, -infinity}) {
        CHECK_THROWS_AS(agents::draw_direction(0.5, bad, engine), std::invalid_argument);
    }
    CHECK(untouched(engine, before));
}

// -------------------------------------------------------------------- size

TEST_CASE("A uniform size stays in its range and reaches every value", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine(17);
    std::set<mm::Quantity> seen;

    for (int i = 0; i < 2000; ++i) {
        const mm::Quantity size =
            agents::draw_size(agents::UniformIntSpec{.min = 3, .max = 7}, engine);
        CHECK(size >= 3);
        CHECK(size <= 7);
        seen.insert(size);
    }
    CHECK(seen == std::set<mm::Quantity>{3, 4, 5, 6, 7});
}

TEST_CASE("A uniform size with min == max is that size", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine();

    CHECK(agents::draw_size(agents::UniformIntSpec{.min = 9, .max = 9}, engine) == 9);
}

TEST_CASE("An exponential size is at least 1 and averages about its mean", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine(19);
    constexpr int draws = 5000;
    double total = 0.0;

    for (int i = 0; i < draws; ++i) {
        const mm::Quantity size = agents::draw_size(agents::ExponentialSpec{.mean = 10.0}, engine);
        CHECK(size >= 1);
        total += static_cast<double>(size);
    }
    // Rounding and the floor at 1 bias the mean very slightly upward.
    CHECK(total / draws == Catch::Approx(10.0).margin(0.8));
}

TEST_CASE("An exponential size is the raw exponential draw rounded to the nearest share",
          "[agents][flow]") {
    // Deterministic, unlike the statistical test above: replay the same
    // stream, take the raw exponential draw by hand, and round it. Rounding
    // down instead of to nearest differs on about half of all draws.
    math::rng::RngEngine engine = fresh_engine(29);
    math::rng::RngEngine replay = engine;

    for (int i = 0; i < 2000; ++i) {
        const double raw = math::dist::exponential(replay, 1.0 / 10.0);
        const auto expected = static_cast<mm::Quantity>(std::max(1.0, std::round(raw)));

        CHECK(agents::draw_size(agents::ExponentialSpec{.mean = 10.0}, engine) == expected);
    }
}

TEST_CASE("A tiny exponential mean still gives a trade of one share", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine();

    for (int i = 0; i < 200; ++i) {
        CHECK(agents::draw_size(agents::ExponentialSpec{.mean = 1.0e-3}, engine) == 1);
    }
}

TEST_CASE("A huge exponential mean is clamped, not overflowed", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine();

    const mm::Quantity size = agents::draw_size(agents::ExponentialSpec{.mean = 1.0e300}, engine);

    CHECK(size == 9'000'000'000'000'000);
}

TEST_CASE("draw_size rejects invalid specs and consumes nothing", "[agents][flow]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;

    CHECK_THROWS_AS(agents::draw_size(agents::UniformIntSpec{.min = 0, .max = 5}, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(agents::draw_size(agents::UniformIntSpec{.min = -3, .max = 5}, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(agents::draw_size(agents::UniformIntSpec{.min = 6, .max = 5}, engine),
                    std::invalid_argument);
    for (const double bad : {0.0, -1.0, quiet_nan, infinity}) {
        CHECK_THROWS_AS(agents::draw_size(agents::ExponentialSpec{.mean = bad}, engine),
                        std::invalid_argument);
    }
    CHECK(untouched(engine, before));
}
