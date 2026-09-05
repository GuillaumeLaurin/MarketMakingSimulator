/// \file
/// Tests for <math/numeric.hpp>: the constexpr scalar helpers (almost_equal,
/// safe_div, sign) and the compiled span helpers (all_close, max_abs_diff).

#include <array>
#include <cmath>
#include <limits>
#include <span>

#include <catch2/catch_test_macros.hpp>

#include <math/numeric.hpp>

namespace {

using mms::math::all_close;
using mms::math::almost_equal;
using mms::math::default_abs_tol;
using mms::math::max_abs_diff;
using mms::math::safe_div;
using mms::math::sign;

constexpr double inf = std::numeric_limits<double>::infinity();
const double nan_value = std::numeric_limits<double>::quiet_NaN();

// --- compile-time --------------------------------------------------------------

static_assert(almost_equal(1.0, 1.0));
static_assert(almost_equal(1.0, 1.0 + 1e-13));
static_assert(!almost_equal(1.0, 1.0 + 1e-10));
static_assert(almost_equal(0.0, 1e-15));
static_assert(!almost_equal(0.0, 1e-13));
static_assert(almost_equal(inf, inf), "exact equality short-circuits, even for infinity");
static_assert(!almost_equal(inf, -inf));
static_assert(almost_equal(1.0, 1.5, 1.0), "a generous rel_tol accepts a wide gap");
static_assert(!almost_equal(1.0, 1.5, 0.1));

static_assert(safe_div(6.0, 3.0, -1.0) == 2.0);
static_assert(safe_div(1.0, 0.0, -1.0) == -1.0);
static_assert(safe_div(1.0, 1e-20, 99.0) == 99.0);
static_assert(safe_div(1.0, -2.0, 0.0) == -0.5);
static_assert(safe_div(1.0, 1e-6, 0.0, 1e-3) == 0.0, "a caller-supplied eps");

static_assert(sign(2.5) == 1);
static_assert(sign(-2.5) == -1);
static_assert(sign(0.0) == 0);
static_assert(sign(-0.0) == 0);
static_assert(sign(inf) == 1);
static_assert(sign(-inf) == -1);

} // namespace

TEST_CASE("almost_equal blends a relative and an absolute bound", "[math][numeric]") {
    CHECK(almost_equal(1.0, 1.0 + 1e-13));
    CHECK_FALSE(almost_equal(1.0, 1.0 + 1e-10));

    CHECK(almost_equal(1e10, 1e10 * (1.0 + 1e-13)));
    CHECK_FALSE(almost_equal(1e10, 1e10 * (1.0 + 1e-10)));

    CHECK(almost_equal(0.0, 0.5 * default_abs_tol));
    CHECK_FALSE(almost_equal(0.0, 100.0 * default_abs_tol));
}

TEST_CASE("almost_equal is symmetric and rejects NaN", "[math][numeric]") {
    CHECK(almost_equal(3.0, 3.0 + 1e-13) == almost_equal(3.0 + 1e-13, 3.0));
    CHECK(almost_equal(inf, inf));
    CHECK_FALSE(almost_equal(inf, -inf));
    CHECK_FALSE(almost_equal(inf, 1.0));
    CHECK_FALSE(almost_equal(nan_value, nan_value));
    CHECK_FALSE(almost_equal(nan_value, 1.0));
}

TEST_CASE("safe_div guards a near-zero denominator", "[math][numeric]") {
    CHECK(safe_div(6.0, 3.0, -1.0) == 2.0);
    CHECK(safe_div(1.0, 0.0, -1.0) == -1.0);
    CHECK(safe_div(1.0, 1e-20, 99.0) == 99.0);
    CHECK(safe_div(1.0, 1e-6, 0.0, 1e-3) == 0.0);

    const double q = safe_div(1.0, 1e-10, 0.0);
    CHECK(q > 9e9);
    CHECK(q < 1.1e10);
}

TEST_CASE("sign follows the sign of x, mapping zeros and NaN to 0", "[math][numeric]") {
    CHECK(sign(2.5) == 1);
    CHECK(sign(-2.5) == -1);
    CHECK(sign(0.0) == 0);
    CHECK(sign(-0.0) == 0);
    CHECK(sign(inf) == 1);
    CHECK(sign(nan_value) == 0);
}

TEST_CASE("all_close compares element-wise and tolerates a length mismatch", "[math][numeric]") {
    const std::array<double, 3> a{1.0, 2.0, 3.0};
    const std::array<double, 3> near{1.0, 2.0 + 1e-13, 3.0};
    const std::array<double, 3> off{1.0, 2.1, 3.0};
    const std::array<double, 2> shorter{1.0, 2.0};

    CHECK(all_close(std::span<const double>{a}, std::span<const double>{a}));
    CHECK(all_close(std::span<const double>{a}, std::span<const double>{near}));
    CHECK_FALSE(all_close(std::span<const double>{a}, std::span<const double>{off}));
    CHECK_FALSE(all_close(std::span<const double>{a}, std::span<const double>{shorter}));
    CHECK(all_close(std::span<const double>{}, std::span<const double>{}));
}

TEST_CASE("max_abs_diff reports the largest gap, or NaN on a size mismatch", "[math][numeric]") {
    const std::array<double, 3> a{1.0, 2.0, 3.0};
    const std::array<double, 3> b{1.0, 2.5, 3.0};
    const std::array<double, 2> shorter{1.0, 2.0};

    CHECK(max_abs_diff(std::span<const double>{a}, std::span<const double>{a}) == 0.0);
    CHECK(max_abs_diff(std::span<const double>{a}, std::span<const double>{b}) == 0.5);
    CHECK(std::isnan(max_abs_diff(std::span<const double>{a}, std::span<const double>{shorter})));
    CHECK(std::isnan(max_abs_diff(std::span<const double>{}, std::span<const double>{})));
}
