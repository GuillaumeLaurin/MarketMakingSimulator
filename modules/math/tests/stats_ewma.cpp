/// \file
/// Tests for EwmaEstimator. The recursion is cross-checked against an
/// independent by-hand replay; the half-life is pinned analytically (after
/// half_life steps the residual weight on the first deviation is exactly 1/2);
/// mean and variance are checked for convergence on known series.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/stats/ewma.hpp>

namespace {

using mms::math::stats::EwmaEstimator;

constexpr double inf = std::numeric_limits<double>::infinity();
const double nan_value = std::numeric_limits<double>::quiet_NaN();

} // namespace

TEST_CASE("EwmaEstimator rejects a non-finite or non-positive half-life", "[stats][ewma]") {
    CHECK_THROWS_AS((EwmaEstimator{0.0}), std::invalid_argument);
    CHECK_THROWS_AS((EwmaEstimator{-1.0}), std::invalid_argument);
    CHECK_THROWS_AS((EwmaEstimator{inf}), std::invalid_argument);
    CHECK_THROWS_AS((EwmaEstimator{nan_value}), std::invalid_argument);
    CHECK_NOTHROW((EwmaEstimator{0.5}));
    CHECK_NOTHROW((EwmaEstimator{10.0}));
}

TEST_CASE("EwmaEstimator is empty before any sample", "[stats][ewma]") {
    const EwmaEstimator e{5.0};
    CHECK(e.sample_count() == 0);
    CHECK(e.half_life() == 5.0);
    CHECK(std::isnan(e.mean()));
    CHECK(std::isnan(e.variance()));
    CHECK(std::isnan(e.stddev()));
}

TEST_CASE("EwmaEstimator seeds on the first sample", "[stats][ewma]") {
    EwmaEstimator e{5.0};
    e.add(7.0);
    CHECK(e.sample_count() == 1);
    CHECK(e.mean() == 7.0);
    CHECK(e.variance() == 0.0);
    CHECK(e.stddev() == 0.0);
}

TEST_CASE("EwmaEstimator half-life: after half_life steps the first deviation is halved",
          "[stats][ewma]") {
    // Seed with A, then feed B for exactly half_life steps. mean_k = B +
    // lambda^(k-1) * (A - B); with k - 1 == half_life, lambda^half_life == 1/2,
    // so the mean lands on the midpoint (A + B) / 2.
    constexpr double half_life = 10.0;
    constexpr double a = 100.0;
    constexpr double b = 0.0;

    EwmaEstimator e{half_life};
    e.add(a);
    for (int i = 0; i < 10; ++i) {
        e.add(b);
    }
    CHECK(e.sample_count() == 11);
    CHECK(e.mean() == Catch::Approx((a + b) / 2.0).margin(1e-9));
}

TEST_CASE("EwmaEstimator matches an independent hand replay", "[stats][ewma]") {
    constexpr double half_life = 7.0;
    const double lambda = std::exp(-std::numbers::ln2 / half_life);
    const std::array<double, 9> xs{2.0, -1.0, 4.0, 4.0, 0.0, 6.0, -3.0, 5.0, 1.0};

    EwmaEstimator e{half_life};
    double mean = 0.0;
    double var = 0.0;
    bool seeded = false;
    for (const double x : xs) {
        e.add(x);
        if (!seeded) {
            mean = x;
            var = 0.0;
            seeded = true;
        } else {
            const double d = x - mean;
            mean += (1.0 - lambda) * d;
            var = (lambda * var) + ((1.0 - lambda) * d * d);
        }
    }
    // identical sequence of operations -> bit-identical
    CHECK(e.mean() == mean);
    CHECK(e.variance() == var);
    CHECK(e.stddev() == std::sqrt(var));
}

TEST_CASE("EwmaEstimator mean converges to a constant series", "[stats][ewma]") {
    EwmaEstimator e{20.0};
    for (int i = 0; i < 500; ++i) {
        e.add(3.0);
    }
    CHECK(e.mean() == Catch::Approx(3.0).margin(1e-6));
    CHECK(e.variance() == Catch::Approx(0.0).margin(1e-9));
}

TEST_CASE("EwmaEstimator variance tracks the spread of a stationary signal", "[stats][ewma]") {
    // x_i = 3 sin(i): long-run mean 0, variance 9 * (1/2) = 4.5.
    EwmaEstimator e{50.0};
    for (int i = 0; i < 4000; ++i) {
        e.add(3.0 * std::sin(static_cast<double>(i)));
    }
    CHECK(e.mean() == Catch::Approx(0.0).margin(0.2));
    CHECK(e.variance() > 3.5);
    CHECK(e.variance() < 5.5);
}

TEST_CASE("EwmaEstimator ignores non-finite samples", "[stats][ewma]") {
    EwmaEstimator e{8.0};
    e.add(1.0);
    e.add(2.0);
    e.add(1.5);
    const std::int64_t n0 = e.sample_count();
    const double mean0 = e.mean();
    const double var0 = e.variance();

    e.add(nan_value);
    e.add(inf);
    e.add(-inf);

    CHECK(e.sample_count() == n0);
    CHECK(e.mean() == mean0);
    CHECK(e.variance() == var0);
}

TEST_CASE("EwmaEstimator is deterministic for a fixed sequence", "[stats][ewma]") {
    EwmaEstimator a{12.0};
    EwmaEstimator b{12.0};
    for (int i = 0; i < 3000; ++i) {
        const double x = std::cos(0.07 * i) * 2.0 + 0.0005 * i;
        a.add(x);
        b.add(x);
    }
    CHECK(a.mean() == b.mean());
    CHECK(a.variance() == b.variance());
}
