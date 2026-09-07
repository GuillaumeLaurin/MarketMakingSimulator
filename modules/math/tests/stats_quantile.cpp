/// \file
/// Tests for the quantile helpers. Three groups: interpolated_quantile (type-7
/// interpolation, hand values and edges), P2State (parabolic / linear predictors
/// and one observe step, in isolation), and PSquareQuantile (the driver:
/// exact while warming up, then convergence of the P^2 estimate).

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/stats/quantile.hpp>

namespace {

using mms::math::stats::interpolated_quantile;
using mms::math::stats::P2State;
using mms::math::stats::PSquareQuantile;

constexpr double inf = std::numeric_limits<double>::infinity();
const double nan_value = std::numeric_limits<double>::quiet_NaN();

template<std::size_t N> double iq(const std::array<double, N>& a, double p) {
    return interpolated_quantile(std::span<const double>{a.data(), a.size()}, p);
}

// Structurally rearranged P^2 parabolic prediction, for cross-checking P2State.
double ref_parabolic(const std::array<double, 5>& q,
                     const std::array<double, 5>& n,
                     std::size_t i,
                     double d) {
    const double left = (n[i] - n[i - 1] + d) * ((q[i + 1] - q[i]) / (n[i + 1] - n[i]));
    const double right = (n[i + 1] - n[i] - d) * ((q[i] - q[i - 1]) / (n[i] - n[i - 1]));
    return q[i] + (d * (left + right) / (n[i + 1] - n[i - 1]));
}

} // namespace

// --- interpolated_quantile --------------------------------------------------

TEST_CASE("interpolated_quantile does type-7 linear interpolation", "[stats][quantile]") {
    const std::array<double, 4> a{1.0, 2.0, 3.0, 4.0};
    CHECK(iq(a, 0.0) == 1.0);
    CHECK(iq(a, 1.0) == 4.0);
    CHECK(iq(a, 0.5) == Catch::Approx(2.5));

    const std::array<double, 3> b{10.0, 20.0, 30.0};
    CHECK(iq(b, 0.25) == Catch::Approx(15.0));

    const std::array<double, 5> c{15.0, 20.0, 35.0, 40.0, 50.0};
    CHECK(iq(c, 0.4) == Catch::Approx(29.0)); // matches numpy percentile 40
}

TEST_CASE("interpolated_quantile edge cases", "[stats][quantile]") {
    const std::array<double, 1> one{5.0};
    CHECK(iq(one, 0.7) == 5.0);

    CHECK(std::isnan(interpolated_quantile(std::span<const double>{}, 0.5)));

    const std::array<double, 2> two{1.0, 2.0};
    CHECK(std::isnan(iq(two, -0.1)));
    CHECK(std::isnan(iq(two, 1.1)));
    CHECK(std::isnan(iq(two, nan_value)));
}

// --- P2State --------------------------------------------------------------

TEST_CASE("P2State::from_sorted lays out the markers", "[stats][quantile][p2]") {
    const P2State s = P2State::from_sorted(0.5, {10.0, 20.0, 30.0, 40.0, 50.0});
    CHECK(s.heights() == std::array<double, 5>{10.0, 20.0, 30.0, 40.0, 50.0});
    CHECK(s.positions() == std::array<double, 5>{1.0, 2.0, 3.0, 4.0, 5.0});
    CHECK(s.desired_positions() == std::array<double, 5>{1.0, 2.0, 3.0, 4.0, 5.0});
    CHECK(s.estimate() == 30.0);
}

TEST_CASE("P2State predictors collapse to a neighbour on a fresh state", "[stats][quantile][p2]") {
    // fresh state has n == {1,2,3,4,5}; for both predictors that reduces to the
    // adjacent marker height.
    const P2State s = P2State::from_sorted(0.5, {1.0, 3.0, 4.0, 8.0, 20.0});
    for (std::size_t i = 1; i <= 3; ++i) {
        CHECK(s.parabolic(i, 1.0) == Catch::Approx(s.heights()[i + 1]));
        CHECK(s.parabolic(i, -1.0) == Catch::Approx(s.heights()[i - 1]));
        CHECK(s.linear(i, 1.0) == Catch::Approx(s.heights()[i + 1]));
        CHECK(s.linear(i, -1.0) == Catch::Approx(s.heights()[i - 1]));
    }
}

TEST_CASE("P2State::observe: one step past the top marker", "[stats][quantile][p2]") {
    P2State s = P2State::from_sorted(0.5, {1.0, 2.0, 3.0, 4.0, 5.0});
    s.observe(6.0); // x >= q[4]: stretches q[4], k = 3, bumps only n[4]
    CHECK(s.heights() == std::array<double, 5>{1.0, 2.0, 3.0, 4.0, 6.0});
    CHECK(s.positions() == std::array<double, 5>{1.0, 2.0, 3.0, 4.0, 6.0});
    CHECK(s.desired_positions() == std::array<double, 5>{1.0, 2.25, 3.5, 4.75, 6.0});
}

TEST_CASE("P2State::parabolic matches an independent formula on a driven state",
          "[stats][quantile][p2]") {
    P2State s = P2State::from_sorted(0.5, {1.0, 2.0, 3.0, 4.0, 5.0});
    for (const double x : {8.0, -2.0, 6.0, 3.5, 9.0, 0.5, 7.0, 4.2, 11.0, 1.5}) {
        s.observe(x);
    }
    // positions are now non-uniform, so the full parabola is exercised
    bool matches = true;
    for (std::size_t i = 1; i <= 3; ++i) {
        for (const double d : {1.0, -1.0}) {
            const double got = s.parabolic(i, d);
            const double want = ref_parabolic(s.heights(), s.positions(), i, d);
            matches = matches && (got == Catch::Approx(want).margin(1e-9));
        }
    }
    CHECK(matches);
}

// --- PSquareQuantile ----------------------------------------------------

TEST_CASE("PSquareQuantile rejects p outside (0, 1)", "[stats][quantile]") {
    CHECK_THROWS_AS((PSquareQuantile{0.0}), std::invalid_argument);
    CHECK_THROWS_AS((PSquareQuantile{1.0}), std::invalid_argument);
    CHECK_THROWS_AS((PSquareQuantile{-0.1}), std::invalid_argument);
    CHECK_THROWS_AS((PSquareQuantile{1.5}), std::invalid_argument);
    CHECK_THROWS_AS((PSquareQuantile{nan_value}), std::invalid_argument);
    CHECK_NOTHROW((PSquareQuantile{0.5}));
    CHECK_NOTHROW((PSquareQuantile{0.9}));
    CHECK_NOTHROW((PSquareQuantile{0.01}));
}

TEST_CASE("PSquareQuantile is exact while warming up", "[stats][quantile]") {
    PSquareQuantile q{0.5};
    CHECK(q.count() == 0);
    CHECK(std::isnan(q.value()));

    q.add(3.0);
    CHECK(q.value() == 3.0);
    q.add(1.0);
    q.add(2.0);
    CHECK(q.count() == 3);
    CHECK(q.value() == Catch::Approx(2.0)); // exact median of {1,2,3}
}

TEST_CASE("PSquareQuantile at exactly five sorted samples returns the middle marker",
          "[stats][quantile]") {
    PSquareQuantile q{0.5};
    for (const double x : {10.0, 20.0, 30.0, 40.0, 50.0}) {
        q.add(x);
    }
    CHECK(q.count() == 5);
    CHECK(q.value() == 30.0);
}

TEST_CASE("PSquareQuantile converges on a well-spread series", "[stats][quantile]") {
    // low-discrepancy golden-ratio sequence, equidistributed on [0, 1)
    PSquareQuantile q10{0.1};
    PSquareQuantile q50{0.5};
    PSquareQuantile q90{0.9};
    double frac = 0.0;
    for (int i = 1; i <= 200'000; ++i) {
        frac += 0.618033988749895;
        frac -= std::floor(frac);
        q10.add(frac);
        q50.add(frac);
        q90.add(frac);
    }
    CHECK(q10.value() == Catch::Approx(0.1).margin(0.02));
    CHECK(q50.value() == Catch::Approx(0.5).margin(0.02));
    CHECK(q90.value() == Catch::Approx(0.9).margin(0.02));
}

TEST_CASE("PSquareQuantile on a monotone-increasing input", "[stats][quantile]") {
    PSquareQuantile q{0.5};
    for (int i = 1; i <= 100; ++i) {
        q.add(static_cast<double>(i));
    }
    // exact median of 1..100 is 50.5; P^2 should be close
    CHECK(q.value() > 47.0);
    CHECK(q.value() < 54.0);
}

TEST_CASE("PSquareQuantile ignores non-finite samples", "[stats][quantile]") {
    PSquareQuantile q{0.5};
    for (const double x : {4.0, 1.0, 9.0, 2.0, 7.0, 3.0, 8.0}) {
        q.add(x);
    }
    const std::int64_t n0 = q.count();
    const double v0 = q.value();

    q.add(nan_value);
    q.add(inf);
    q.add(-inf);

    CHECK(q.count() == n0);
    CHECK(q.value() == v0);
}

TEST_CASE("PSquareQuantile is deterministic for a fixed sequence", "[stats][quantile]") {
    PSquareQuantile a{0.7};
    PSquareQuantile b{0.7};
    double frac = 0.3;
    for (int i = 0; i < 50'000; ++i) {
        frac += 0.414213562373095;
        frac -= std::floor(frac);
        a.add(frac);
        b.add(frac);
    }
    CHECK(a.value() == b.value());
    CHECK(a.count() == b.count());
}
