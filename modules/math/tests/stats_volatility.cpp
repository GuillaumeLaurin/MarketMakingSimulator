/// \file
/// Tests for the volatility estimators and their type-erased wrapper. Each model
/// is checked against an independent computation; AnyVolatilityEstimator is
/// checked for forwarding, independent copy/move, and SBO fit.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/stats/ewma.hpp>
#include <math/stats/traits.hpp>
#include <math/stats/volatility.hpp>

namespace {

using mms::math::stats::AnyVolatilityEstimator;
using mms::math::stats::ConstantVolEstimator;
using mms::math::stats::EwmaEstimator;
using mms::math::stats::EwmaVolEstimator;
using mms::math::stats::RollingVolEstimator;
using mms::math::stats::VolatilityEstimator;

constexpr double inf = std::numeric_limits<double>::infinity();
const double nan_value = std::numeric_limits<double>::quiet_NaN();

static_assert(VolatilityEstimator<ConstantVolEstimator>);
static_assert(VolatilityEstimator<EwmaVolEstimator>);
static_assert(VolatilityEstimator<RollingVolEstimator>);
static_assert(VolatilityEstimator<AnyVolatilityEstimator>);

static_assert(sizeof(ConstantVolEstimator) <= AnyVolatilityEstimator::buffer_capacity);
static_assert(sizeof(EwmaVolEstimator) <= AnyVolatilityEstimator::buffer_capacity);
static_assert(sizeof(RollingVolEstimator) <= AnyVolatilityEstimator::buffer_capacity);

double sample_stddev(std::span<const double> xs) {
    if (xs.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double sum = 0.0;
    for (const double x : xs) {
        sum += x;
    }
    const double mean = sum / static_cast<double>(xs.size());
    double m2 = 0.0;
    for (const double x : xs) {
        m2 += (x - mean) * (x - mean);
    }
    return std::sqrt(m2 / static_cast<double>(xs.size() - 1));
}

template<class E> bool any_forwards(E model) {
    E naked = model;
    AnyVolatilityEstimator any{std::move(model)};
    bool ok = true;
    double frac = 0.11;
    for (int i = 0; i < 300; ++i) {
        frac += 0.37;
        frac -= std::floor(frac);
        any.update(frac);
        naked.update(frac);
        ok = ok && (any.sigma() == naked.sigma() ||
                    (std::isnan(any.sigma()) && std::isnan(naked.sigma())));
        ok = ok && (any.sample_count() == naked.sample_count());
    }
    return ok;
}

} // namespace

// --- ConstantVolEstimator -------------------------------------------------

TEST_CASE("ConstantVolEstimator returns its config sigma no matter the input",
          "[stats][volatility]") {
    CHECK_THROWS_AS((ConstantVolEstimator{0.0}), std::invalid_argument);
    CHECK_THROWS_AS((ConstantVolEstimator{-1.0}), std::invalid_argument);
    CHECK_THROWS_AS((ConstantVolEstimator{inf}), std::invalid_argument);
    CHECK_THROWS_AS((ConstantVolEstimator{nan_value}), std::invalid_argument);

    ConstantVolEstimator e{2.5};
    CHECK(e.sigma() == 2.5);
    for (const double x : {100.0, -50.0, 0.0, 1e6}) {
        e.update(x);
    }
    CHECK(e.sigma() == 2.5);
    CHECK(e.sample_count() == 4);

    e.update(nan_value);
    e.update(inf);
    CHECK(e.sample_count() == 4); // non-finite not counted
}

// --- EwmaVolEstimator ---------------------------------------------------

TEST_CASE("EwmaVolEstimator is the EWMA stddev", "[stats][volatility]") {
    CHECK_THROWS_AS((EwmaVolEstimator{0.0}), std::invalid_argument);
    CHECK_THROWS_AS((EwmaVolEstimator{nan_value}), std::invalid_argument);

    EwmaVolEstimator vol{15.0};
    EwmaEstimator ewma{15.0};
    CHECK(std::isnan(vol.sigma())); // NaN before the first sample
    CHECK(vol.half_life() == 15.0);

    bool same = true;
    double frac = 0.2;
    for (int i = 0; i < 500; ++i) {
        frac += 0.41;
        frac -= std::floor(frac);
        vol.update(frac);
        ewma.add(frac);
        same = same && (vol.sigma() == ewma.stddev());
    }
    CHECK(same);
    CHECK(vol.sample_count() == ewma.sample_count());
}

// --- RollingVolEstimator ----------------------------------------------

TEST_CASE("RollingVolEstimator rejects a window below 2", "[stats][volatility]") {
    CHECK_THROWS_AS((RollingVolEstimator{0}), std::invalid_argument);
    CHECK_THROWS_AS((RollingVolEstimator{1}), std::invalid_argument);
    CHECK_NOTHROW((RollingVolEstimator{2}));
    CHECK_NOTHROW((RollingVolEstimator{64}));
}

TEST_CASE("RollingVolEstimator is the sample stddev of the last window values",
          "[stats][volatility]") {
    RollingVolEstimator e{3};
    CHECK(e.window() == 3);
    CHECK(std::isnan(e.sigma())); // fewer than 2 samples

    e.update(1.0);
    CHECK(std::isnan(e.sigma()));
    e.update(2.0);
    { // window {1,2}
        const std::array<double, 2> w{1.0, 2.0};
        CHECK(e.sigma() == Catch::Approx(sample_stddev(w)));
    }
    e.update(3.0);
    { // window {1,2,3}
        const std::array<double, 3> w{1.0, 2.0, 3.0};
        CHECK(e.sigma() == Catch::Approx(sample_stddev(w)));
        CHECK(e.sigma() == Catch::Approx(1.0));
    }
    e.update(4.0);
    e.update(5.0);
    { // window slid to {3,4,5}
        const std::array<double, 3> w{3.0, 4.0, 5.0};
        CHECK(e.sigma() == Catch::Approx(sample_stddev(w)));
        CHECK(e.sigma() == Catch::Approx(1.0));
    }
    CHECK(e.sample_count() == 5); // total updates, not capped at window
}

TEST_CASE("RollingVolEstimator sigma() is idempotent between updates and caches",
          "[stats][volatility]") {
    RollingVolEstimator e{4};
    for (const double x : {2.0, 5.0, 1.0, 8.0}) {
        e.update(x);
    }
    const double s1 = e.sigma();
    const double s2 = e.sigma(); // served from cache, identical
    CHECK(s1 == s2);
    e.update(3.0);
    CHECK(e.sigma() != s1); // recomputed after the window changed
}

TEST_CASE("RollingVolEstimator ignores non-finite updates", "[stats][volatility]") {
    RollingVolEstimator e{5};
    for (const double x : {1.0, 2.0, 3.0, 4.0}) {
        e.update(x);
    }
    const double s0 = e.sigma();
    const std::int64_t n0 = e.sample_count();
    e.update(nan_value);
    e.update(inf);
    e.update(-inf);
    CHECK(e.sigma() == s0);
    CHECK(e.sample_count() == n0);
}

// --- AnyVolatilityEstimator ------------------------------------------

TEST_CASE("AnyVolatilityEstimator forwards to the wrapped estimator", "[stats][volatility][any]") {
    CHECK(any_forwards(ConstantVolEstimator{1.5}));
    CHECK(any_forwards(EwmaVolEstimator{12.0}));
    CHECK(any_forwards(RollingVolEstimator{8}));
}

TEST_CASE("AnyVolatilityEstimator copy is an independent deep copy", "[stats][volatility][any]") {
    AnyVolatilityEstimator a{RollingVolEstimator{4}};
    a.update(1.0);
    a.update(2.0);
    a.update(3.0);

    AnyVolatilityEstimator b = a; // deep copy of the ring buffer
    const double b_sigma_before = b.sigma();

    for (int i = 0; i < 20; ++i) {
        a.update(100.0);
    }
    CHECK(b.sigma() == b_sigma_before); // b untouched by a's updates
    CHECK(b.sample_count() == 3);
    CHECK(a.sample_count() == 23);
}

TEST_CASE("AnyVolatilityEstimator move and reassignment", "[stats][volatility][any]") {
    AnyVolatilityEstimator src{ConstantVolEstimator{3.0}};
    const AnyVolatilityEstimator dst{std::move(src)};
    CHECK(dst.sigma() == 3.0);

    AnyVolatilityEstimator e{ConstantVolEstimator{1.0}};
    CHECK(e.sigma() == 1.0);
    e = AnyVolatilityEstimator{ConstantVolEstimator{5.0}};
    CHECK(e.sigma() == 5.0);
}

TEST_CASE("AnyVolatilityEstimator wrapping an Any is a plain copy, not a re-wrap",
          "[stats][volatility][any]") {
    AnyVolatilityEstimator a{EwmaVolEstimator{10.0}};
    a.update(2.0);
    a.update(4.0);
    AnyVolatilityEstimator b{a}; // copy ctor, not the template ctor
    CHECK(b.sigma() == a.sigma());
    CHECK(b.sample_count() == a.sample_count());
}
