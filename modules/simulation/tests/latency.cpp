/// \file
/// Tests for simulation/latency.hpp: which DelaySpecs are valid, that a
/// drawn delay follows its distribution (statistically, on a fixed seed, so
/// the checks are deterministic), the exact number of engine words each kind
/// consumes (a constant delay none, the others one: that is what keeps a run
/// reproducible), that a delay is never negative and never infinite, and that
/// a rejected call leaves the engine untouched.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/dist/exponential.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/seed.hpp>
#include <math/rng/stream.hpp>
#include <simulation/delay_spec.hpp>
#include <simulation/latency.hpp>

namespace sim = mms::simulation;
namespace math = mms::math;

namespace {

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

math::rng::RngEngine fresh_engine(std::uint64_t seed = 1) {
    const math::rng::RngHub hub{math::rng::Seed{seed}};
    return hub.engine_for(
        {.subsystem = math::rng::Subsystem::Latency, .asset = math::rng::AssetKey{0}});
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

sim::DelaySpec constant(double delay) {
    return sim::ConstantDelaySpec{.delay = delay};
}

sim::DelaySpec uniform(double min, double max) {
    return sim::UniformDelaySpec{.min = min, .max = max};
}

sim::DelaySpec exponential(double mean) {
    return sim::ExponentialDelaySpec{.mean = mean};
}

} // namespace

// -------------------------------------------------------------- validation

TEST_CASE("validate_delay_spec accepts every well-formed spec", "[simulation][latency]") {
    CHECK_NOTHROW(sim::validate_delay_spec(constant(0.0)));
    CHECK_NOTHROW(sim::validate_delay_spec(constant(0.5)));
    CHECK_NOTHROW(sim::validate_delay_spec(uniform(0.0, 0.0)));
    CHECK_NOTHROW(sim::validate_delay_spec(uniform(0.0, 1.0)));
    CHECK_NOTHROW(sim::validate_delay_spec(uniform(0.25, 0.25)));
    CHECK_NOTHROW(sim::validate_delay_spec(exponential(0.01)));
}

TEST_CASE("validate_delay_spec rejects a constant delay that is negative or not finite",
          "[simulation][latency]") {
    for (const double bad : {-1.0, -1.0e-12, quiet_nan, infinity, -infinity}) {
        CHECK_THROWS_AS(sim::validate_delay_spec(constant(bad)), std::invalid_argument);
    }
}

TEST_CASE("validate_delay_spec rejects a uniform range that is negative, reversed or not finite",
          "[simulation][latency]") {
    CHECK_THROWS_AS(sim::validate_delay_spec(uniform(-0.1, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(sim::validate_delay_spec(uniform(2.0, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(sim::validate_delay_spec(uniform(quiet_nan, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(sim::validate_delay_spec(uniform(0.0, quiet_nan)), std::invalid_argument);
    CHECK_THROWS_AS(sim::validate_delay_spec(uniform(0.0, infinity)), std::invalid_argument);
}

TEST_CASE("validate_delay_spec rejects an exponential mean that is not finite and positive",
          "[simulation][latency]") {
    for (const double bad : {0.0, -1.0, quiet_nan, infinity}) {
        CHECK_THROWS_AS(sim::validate_delay_spec(exponential(bad)), std::invalid_argument);
    }
}

// ---------------------------------------------------------------- constant

TEST_CASE("A constant delay is exactly its value and consumes no engine word",
          "[simulation][latency]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;

    CHECK(sim::draw_delay(constant(0.0), engine) == 0.0);
    CHECK(sim::draw_delay(constant(0.125), engine) == 0.125);
    CHECK(untouched(engine, before));
}

// ----------------------------------------------------------------- uniform

TEST_CASE("A uniform delay stays in [min, max) and averages the midpoint",
          "[simulation][latency]") {
    math::rng::RngEngine engine = fresh_engine(5);
    constexpr int draws = 20000;
    double total = 0.0;

    for (int i = 0; i < draws; ++i) {
        const double delay = sim::draw_delay(uniform(0.2, 0.6), engine);
        CHECK(delay >= 0.2);
        CHECK(delay < 0.6);
        total += delay;
    }
    CHECK(total / draws == Catch::Approx(0.4).margin(0.005));
}

TEST_CASE("A degenerate uniform delay is that value", "[simulation][latency]") {
    math::rng::RngEngine engine = fresh_engine();

    CHECK(sim::draw_delay(uniform(0.3, 0.3), engine) == 0.3);
    CHECK(sim::draw_delay(uniform(0.0, 0.0), engine) == 0.0);
}

TEST_CASE("A uniform delay consumes exactly one engine word", "[simulation][latency]") {
    math::rng::RngEngine engine = fresh_engine(3);
    const math::rng::RngEngine before = engine;

    static_cast<void>(sim::draw_delay(uniform(0.0, 1.0), engine));

    CHECK(advanced_by(engine, before, 1));
}

// ------------------------------------------------------------- exponential

TEST_CASE("An exponential delay averages its mean and is never negative", "[simulation][latency]") {
    math::rng::RngEngine engine = fresh_engine(7);
    constexpr int draws = 20000;
    double total = 0.0;

    for (int i = 0; i < draws; ++i) {
        const double delay = sim::draw_delay(exponential(0.5), engine);
        CHECK(delay >= 0.0);
        total += delay;
    }
    CHECK(total / draws == Catch::Approx(0.5).margin(0.02));
}

TEST_CASE("An exponential delay is the raw exponential draw at rate 1 / mean",
          "[simulation][latency]") {
    // Deterministic: replay the same stream and draw by hand. A mean used
    // as the rate (instead of 1 / mean) would differ on every single draw.
    math::rng::RngEngine engine = fresh_engine(11);
    math::rng::RngEngine replay = engine;

    for (int i = 0; i < 2000; ++i) {
        const double expected = math::dist::exponential(replay, 1.0 / 0.25);

        CHECK(sim::draw_delay(exponential(0.25), engine) == expected);
    }
}

TEST_CASE("An exponential delay consumes exactly one engine word", "[simulation][latency]") {
    math::rng::RngEngine engine = fresh_engine(3);
    const math::rng::RngEngine before = engine;

    static_cast<void>(sim::draw_delay(exponential(1.0), engine));

    CHECK(advanced_by(engine, before, 1));
}

TEST_CASE("A huge exponential mean is clamped, never scheduling a request at infinity",
          "[simulation][latency]") {
    // With a mean this large the raw draw overflows to +infinity for about
    // one draw in six; every result must still be finite.
    math::rng::RngEngine engine = fresh_engine(13);
    int clamped = 0;

    for (int i = 0; i < 300; ++i) {
        const double delay = sim::draw_delay(exponential(1.0e308), engine);
        CHECK(std::isfinite(delay));
        CHECK(delay >= 0.0);
        if (delay == std::numeric_limits<double>::max()) {
            ++clamped;
        }
    }
    CHECK(clamped > 0); // the clamp really was exercised
}

TEST_CASE("A subnormal exponential mean is rejected by math's own rate check, consuming nothing",
          "[simulation][latency]") {
    // Valid for validate_delay_spec (finite, > 0), but 1 / mean overflows to
    // infinity, which math::dist::exponential refuses as a rate.
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;

    CHECK_THROWS_AS(sim::draw_delay(exponential(1.0e-310), engine), std::invalid_argument);
    CHECK(untouched(engine, before));
}

// ------------------------------------------------------------------ shared

TEST_CASE("draw_delay rejects an invalid spec and consumes nothing", "[simulation][latency]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;

    CHECK_THROWS_AS(sim::draw_delay(constant(-1.0), engine), std::invalid_argument);
    CHECK_THROWS_AS(sim::draw_delay(uniform(2.0, 1.0), engine), std::invalid_argument);
    CHECK_THROWS_AS(sim::draw_delay(uniform(-1.0, 1.0), engine), std::invalid_argument);
    CHECK_THROWS_AS(sim::draw_delay(exponential(0.0), engine), std::invalid_argument);
    CHECK_THROWS_AS(sim::draw_delay(exponential(quiet_nan), engine), std::invalid_argument);
    CHECK(untouched(engine, before));
}

TEST_CASE("The same engine state gives the same delay for every kind", "[simulation][latency]") {
    for (const sim::DelaySpec& spec : {constant(0.1), uniform(0.0, 1.0), exponential(0.3)}) {
        math::rng::RngEngine first = fresh_engine(17);
        math::rng::RngEngine second = fresh_engine(17);

        for (int i = 0; i < 50; ++i) {
            CHECK(sim::draw_delay(spec, first) == sim::draw_delay(spec, second));
        }
    }
}

TEST_CASE("Different engine states give different random delays", "[simulation][latency]") {
    math::rng::RngEngine first = fresh_engine(17);
    math::rng::RngEngine second = fresh_engine(18);

    CHECK(sim::draw_delay(exponential(0.3), first) != sim::draw_delay(exponential(0.3), second));
}

TEST_CASE("A delay is never negative and never infinite, whatever the kind",
          "[simulation][latency]") {
    math::rng::RngEngine engine = fresh_engine(19);

    for (int i = 0; i < 1000; ++i) {
        for (const sim::DelaySpec& spec : {constant(0.0), uniform(0.0, 2.0), exponential(2.0)}) {
            const double delay = sim::draw_delay(spec, engine);
            CHECK(delay >= 0.0);
            CHECK(std::isfinite(delay));
        }
    }
}
