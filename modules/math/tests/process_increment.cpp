/// \file
/// Tests for correlated_normal_increment: cross-checked against an
/// independent manual computation (draw z by hand from a copy of each
/// engine, apply L and sqrt(dt) by hand) rather than hand-verifying Philox
/// itself, plus the lower-triangular-only and word-consumption contracts.

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/dist/normal.hpp>
#include <math/process/increment.hpp>
#include <math/rng/stream.hpp>

namespace {

using mms::math::Matrix;
using mms::math::Vector;
using mms::math::process::correlated_normal_increment;
namespace rng = mms::math::rng;

std::vector<rng::RngEngine> engines_for(const rng::RngHub& hub, int n) {
    std::vector<rng::RngEngine> engines;
    for (int i = 0; i < n; ++i) {
        engines.push_back(hub.engine_for({.subsystem = rng::Subsystem::Price,
                                          .asset = rng::AssetKey{static_cast<std::uint64_t>(i)}}));
    }
    return engines;
}

double max_abs_diff(const Vector& a, const Vector& b) {
    return (a - b).cwiseAbs().maxCoeff();
}

} // namespace

TEST_CASE("correlated_normal_increment matches a manual draw-then-apply-L computation",
          "[process][increment]") {
    const rng::RngHub hub{rng::Seed{0x5EED'C0DEULL}};
    std::vector<rng::RngEngine> engines = engines_for(hub, 3);
    std::vector<rng::RngEngine> reference = engines; // independent copy, same state

    Matrix chol(3, 3);
    chol << 2.0, 0.0, 0.0, 0.5, 1.5, 0.0, 0.3, -0.2, 1.0;
    constexpr double dt = 0.25;

    const Vector actual = correlated_normal_increment(chol, engines, dt);

    Vector z(3);
    for (int i = 0; i < 3; ++i) {
        z(i) = mms::math::dist::standard_normal(reference[static_cast<std::size_t>(i)]);
    }
    const Vector expected = chol.triangularView<Eigen::Lower>() * z * std::sqrt(dt);

    CHECK(max_abs_diff(actual, expected) < 1e-15);
}

TEST_CASE("correlated_normal_increment consumes exactly one word per engine",
          "[process][increment]") {
    const rng::RngHub hub{rng::Seed{0xFACE'FEEDULL}};
    std::vector<rng::RngEngine> engines = engines_for(hub, 2);
    std::vector<rng::RngEngine> reference = engines;

    const Matrix chol = Matrix::Identity(2, 2);
    (void)correlated_normal_increment(chol, engines, 1.0);

    for (auto& e : reference) {
        (void)mms::math::dist::standard_normal(e); // exactly one draw
    }

    CHECK(engines[0] == reference[0]);
    CHECK(engines[1] == reference[1]);
}

TEST_CASE("correlated_normal_increment with an identity L is sqrt(dt) * z",
          "[process][increment]") {
    const rng::RngHub hub{rng::Seed{0x1234'5678ULL}};
    std::vector<rng::RngEngine> engines = engines_for(hub, 2);
    std::vector<rng::RngEngine> reference = engines;

    constexpr double dt = 4.0; // sqrt(dt) = 2, easy to eyeball
    const Vector actual = correlated_normal_increment(Matrix::Identity(2, 2), engines, dt);

    Vector expected(2);
    for (int i = 0; i < 2; ++i) {
        expected(i) =
            2.0 * mms::math::dist::standard_normal(reference[static_cast<std::size_t>(i)]);
    }

    CHECK(max_abs_diff(actual, expected) < 1e-15);
}

TEST_CASE("correlated_normal_increment reads only the lower-triangular part of chol",
          "[process][increment]") {
    const rng::RngHub hub{rng::Seed{0xABCD'EF01ULL}};
    std::vector<rng::RngEngine> lower_engines = engines_for(hub, 2);
    std::vector<rng::RngEngine> garbage_engines = lower_engines;

    Matrix lower(2, 2);
    lower << 1.0, 0.0, 0.5, 2.0;
    Matrix garbage_upper = lower;
    garbage_upper(0, 1) = 999.0; // must be ignored

    const Vector from_lower = correlated_normal_increment(lower, lower_engines, 1.0);
    const Vector from_garbage = correlated_normal_increment(garbage_upper, garbage_engines, 1.0);

    CHECK(max_abs_diff(from_lower, from_garbage) < 1e-15);
}

TEST_CASE("correlated_normal_increment at dt == 0 is the zero vector", "[process][increment]") {
    const rng::RngHub hub{rng::Seed{0x0BAD'F00DULL}};
    std::vector<rng::RngEngine> engines = engines_for(hub, 2);

    const Vector actual = correlated_normal_increment(Matrix::Identity(2, 2), engines, 0.0);

    CHECK(actual(0) == Catch::Approx(0.0));
    CHECK(actual(1) == Catch::Approx(0.0));
}

TEST_CASE("correlated_normal_increment rejects a negative or non-finite dt",
          "[process][increment]") {
    const rng::RngHub hub{rng::Seed{0xDEAD'BEEFULL}};
    const Matrix chol = Matrix::Identity(2, 2);
    std::vector<rng::RngEngine> engines_a = engines_for(hub, 2);
    std::vector<rng::RngEngine> engines_b = engines_for(hub, 2);
    std::vector<rng::RngEngine> engines_c = engines_for(hub, 2);
    std::vector<rng::RngEngine> engines_d = engines_for(hub, 2);

    CHECK_THROWS_AS(correlated_normal_increment(chol, engines_a, -1.0), std::invalid_argument);
    CHECK_THROWS_AS(
        correlated_normal_increment(chol, engines_b, std::numeric_limits<double>::quiet_NaN()),
        std::invalid_argument);
    CHECK_THROWS_AS(
        correlated_normal_increment(chol, engines_c, -std::numeric_limits<double>::infinity()),
        std::invalid_argument);
    CHECK_NOTHROW(correlated_normal_increment(chol, engines_d, 0.0));
}

TEST_CASE("correlated_normal_increment handles a single asset", "[process][increment]") {
    const rng::RngHub hub{rng::Seed{0x5A5A'5A5AULL}};
    std::vector<rng::RngEngine> engines = engines_for(hub, 1);
    std::vector<rng::RngEngine> reference = engines;

    Matrix chol(1, 1);
    chol << 3.0;
    constexpr double dt = 2.0;

    const Vector actual = correlated_normal_increment(chol, engines, dt);
    const double expected = 3.0 * std::sqrt(dt) * mms::math::dist::standard_normal(reference[0]);

    CHECK(actual(0) == Catch::Approx(expected));
}
