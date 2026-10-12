/// \file
/// Tests for CorrelatedRandomWalk. advance() is cross-checked against
/// mu*dt + correlated_normal_increment() computed independently over a copy
/// of the same engines, over several successive ticks. Also covers the
/// construction-time size validation, reset()'s restoration of both prices
/// and engine state, and the zero-chol deterministic-drift edge case.

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/process/correlated_random_walk.hpp>
#include <math/process/increment.hpp>
#include <math/rng/stream.hpp>

namespace {

using mms::math::Matrix;
using mms::math::Vector;
using mms::math::process::correlated_normal_increment;
using mms::math::process::CorrelatedRandomWalk;
using mms::math::process::Dynamics;
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

TEST_CASE("CorrelatedRandomWalk rejects inconsistent construction sizes",
          "[process][correlated_random_walk]") {
    const rng::RngHub hub{rng::Seed{0x1111'1111ULL}};

    Vector prices(2);
    prices << 100.0, 50.0;
    Vector mu(2);
    mu << 0.01, -0.02;
    Matrix chol(2, 2);
    chol << 1.0, 0.0, 0.5, 1.0;

    CHECK_THROWS_AS(
        CorrelatedRandomWalk(prices, Vector(3), chol, engines_for(hub, 2), Dynamics::Arithmetic),
        std::invalid_argument); // mu wrong size
    CHECK_THROWS_AS(
        CorrelatedRandomWalk(prices, mu, Matrix(3, 3), engines_for(hub, 2), Dynamics::Arithmetic),
        std::invalid_argument); // chol wrong size
    CHECK_THROWS_AS(
        CorrelatedRandomWalk(prices, mu, chol, engines_for(hub, 3), Dynamics::Arithmetic),
        std::invalid_argument); // wrong number of engines
    CHECK_THROWS_AS(
        CorrelatedRandomWalk(Vector(3), mu, chol, engines_for(hub, 2), Dynamics::Arithmetic),
        std::invalid_argument); // initial_prices wrong size

    CHECK_NOTHROW(
        CorrelatedRandomWalk(prices, mu, chol, engines_for(hub, 2), Dynamics::Arithmetic));
}

TEST_CASE("CorrelatedRandomWalk exposes the initial prices and asset count before any advance",
          "[process][correlated_random_walk]") {
    const rng::RngHub hub{rng::Seed{0x2222'2222ULL}};
    Vector prices(3);
    prices << 100.0, 50.0, 25.0;
    Vector mu = Vector::Zero(3);
    Matrix chol = Matrix::Identity(3, 3);

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, 3), Dynamics::Arithmetic};

    CHECK(walk.asset_count() == 3);
    CHECK(max_abs_diff(walk.prices(), prices) < 1e-15);
}

TEST_CASE("CorrelatedRandomWalk::advance matches mu*dt + correlated_normal_increment over "
          "several ticks",
          "[process][correlated_random_walk]") {
    const rng::RngHub hub{rng::Seed{0x3333'3333ULL}};
    std::vector<rng::RngEngine> engines = engines_for(hub, 2);
    std::vector<rng::RngEngine> reference = engines; // independent copy, same starting state

    Vector prices(2);
    prices << 100.0, 200.0;
    Vector mu(2);
    mu << 0.05, -0.10;
    Matrix chol(2, 2);
    chol << 1.2, 0.0, 0.3, 0.8;
    constexpr double dt = 0.1;

    CorrelatedRandomWalk walk{prices, mu, chol, std::move(engines), Dynamics::Arithmetic};

    Vector expected = prices;
    for (int tick = 0; tick < 5; ++tick) {
        walk.advance(dt);
        expected += (mu * dt) + correlated_normal_increment(chol, reference, dt);
        CHECK(max_abs_diff(walk.prices(), expected) < 1e-12);
    }
}

TEST_CASE("CorrelatedRandomWalk::reset restores both prices and engine state",
          "[process][correlated_random_walk]") {
    const rng::RngHub hub{rng::Seed{0x4444'4444ULL}};
    Vector prices(2);
    prices << 10.0, 20.0;
    Vector mu(2);
    mu << 0.01, 0.02;
    Matrix chol = Matrix::Identity(2, 2);

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, 2), Dynamics::Arithmetic};

    walk.advance(0.5);
    walk.advance(0.5);
    const Vector after_first_run = walk.prices();

    walk.reset();
    CHECK(max_abs_diff(walk.prices(), prices) < 1e-15);

    walk.advance(0.5);
    walk.advance(0.5);
    // same dt sequence from the same reset state must reproduce the same path
    CHECK(max_abs_diff(walk.prices(), after_first_run) < 1e-12);
}

TEST_CASE("CorrelatedRandomWalk::advance(0.0) leaves prices unchanged",
          "[process][correlated_random_walk]") {
    const rng::RngHub hub{rng::Seed{0x5555'5555ULL}};
    Vector prices(2);
    prices << 42.0, 7.0;
    Vector mu(2);
    mu << 0.1, -0.1;
    Matrix chol = Matrix::Identity(2, 2);

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, 2), Dynamics::Arithmetic};
    walk.advance(0.0);

    CHECK(walk.prices()(0) == Catch::Approx(42.0));
    CHECK(walk.prices()(1) == Catch::Approx(7.0));
}

TEST_CASE("CorrelatedRandomWalk::advance rejects a negative dt via the PriceProcess contract",
          "[process][correlated_random_walk]") {
    const rng::RngHub hub{rng::Seed{0x7777'7777ULL}};
    Vector prices(2);
    prices << 10.0, 20.0;
    Vector mu(2);
    mu << 0.01, 0.02;
    Matrix chol = Matrix::Identity(2, 2);

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, 2), Dynamics::Arithmetic};

    CHECK_THROWS_AS(walk.advance(-1.0), std::invalid_argument);
    // the rejected call must not have mutated prices or consumed any engine word
    CHECK(max_abs_diff(walk.prices(), prices) < 1e-15);
}

TEST_CASE("CorrelatedRandomWalk with chol == 0 is pure deterministic drift",
          "[process][correlated_random_walk]") {
    const rng::RngHub hub{rng::Seed{0x6666'6666ULL}};
    Vector prices(2);
    prices << 100.0, 100.0;
    Vector mu(2);
    mu << 1.0, -2.0;
    const Matrix chol = Matrix::Zero(2, 2);

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, 2), Dynamics::Arithmetic};
    walk.advance(2.0);

    CHECK(walk.prices()(0) == Catch::Approx(102.0));
    CHECK(walk.prices()(1) == Catch::Approx(96.0));
}

TEST_CASE("CorrelatedRandomWalk with geometric dynamics rejects initial prices that are not "
          "finite and positive",
          "[process][correlated_random_walk][geometric]") {
    const rng::RngHub hub{rng::Seed{0x8888'0001ULL}};
    Vector mu = Vector::Zero(2);
    const Matrix chol = Matrix::Identity(2, 2);

    for (const double bad : {0.0,
                             -1.0,
                             std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        Vector prices(2);
        prices << 100.0, bad;
        CHECK_THROWS_AS(
            CorrelatedRandomWalk(prices, mu, chol, engines_for(hub, 2), Dynamics::Geometric),
            std::invalid_argument);
        // the arithmetic walk does not care about the sign of a price
        if (std::isfinite(bad)) {
            CHECK_NOTHROW(
                CorrelatedRandomWalk(prices, mu, chol, engines_for(hub, 2), Dynamics::Arithmetic));
        }
    }
}

TEST_CASE("CorrelatedRandomWalk geometric advance matches p * exp((mu - variance/2) dt + "
          "increment) over several ticks",
          "[process][correlated_random_walk][geometric]") {
    const rng::RngHub hub{rng::Seed{0x8888'0002ULL}};
    std::vector<rng::RngEngine> engines = engines_for(hub, 2);
    std::vector<rng::RngEngine> reference = engines;

    Vector prices(2);
    prices << 100.0, 200.0;
    Vector mu(2);
    mu << 0.05, -0.10;
    Matrix chol(2, 2);
    chol << 0.3, 0.0, 0.1, 0.2;
    constexpr double dt = 0.1;
    // variance is the diagonal of L L^T: the cross term of row 1 counts
    Vector variance(2);
    variance << 0.3 * 0.3, (0.1 * 0.1) + (0.2 * 0.2);

    CorrelatedRandomWalk walk{prices, mu, chol, std::move(engines), Dynamics::Geometric};

    Vector expected = prices;
    for (int tick = 0; tick < 5; ++tick) {
        walk.advance(dt);
        const Vector log_return =
            ((mu - 0.5 * variance) * dt) + correlated_normal_increment(chol, reference, dt);
        expected = expected.cwiseProduct(log_return.array().exp().matrix());
        CHECK(max_abs_diff(walk.prices(), expected) < 1e-12);
    }
}

TEST_CASE("CorrelatedRandomWalk geometric with chol == 0 grows at exp(mu dt)",
          "[process][correlated_random_walk][geometric]") {
    const rng::RngHub hub{rng::Seed{0x8888'0003ULL}};
    Vector prices(2);
    prices << 100.0, 100.0;
    Vector mu(2);
    mu << 0.5, -0.5;

    CorrelatedRandomWalk walk{
        prices, mu, Matrix::Zero(2, 2), engines_for(hub, 2), Dynamics::Geometric};
    walk.advance(2.0);

    CHECK(walk.prices()(0) == Catch::Approx(100.0 * std::exp(1.0)));
    CHECK(walk.prices()(1) == Catch::Approx(100.0 * std::exp(-1.0)));
}

TEST_CASE("A random walk has no factors, before or after a step",
          "[process][correlated_random_walk]") {
    const rng::RngHub hub{rng::Seed{0x8888'0009ULL}};
    Vector prices(2);
    prices << 100.0, 50.0;
    CorrelatedRandomWalk walk{
        prices, Vector::Zero(2), Matrix::Identity(2, 2), engines_for(hub, 2), Dynamics::Geometric};
    mms::math::process::PriceProcess& process = walk;

    CHECK(process.factor_increments().size() == 0);
    process.advance(1.0);
    CHECK(process.factor_increments().size() == 0);
}

TEST_CASE("CorrelatedRandomWalk geometric prices stay positive however volatile",
          "[process][correlated_random_walk][geometric]") {
    const rng::RngHub hub{rng::Seed{0x8888'0004ULL}};
    Vector prices(2);
    prices << 1.0, 1.0;
    Vector mu = Vector::Zero(2);
    const Matrix chol = 3.0 * Matrix::Identity(2, 2);

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, 2), Dynamics::Geometric};
    for (int tick = 0; tick < 500; ++tick) {
        walk.advance(1.0);
        CHECK(walk.prices().minCoeff() >= 0.0);
    }
}

TEST_CASE("CorrelatedRandomWalk geometric mu is the expected return rate",
          "[process][correlated_random_walk][geometric]") {
    // 2000 independent assets, one step: the mean of p1 / p0 must be exp(mu dt)
    // (without the Ito correction it would be exp((mu + sigma^2/2) dt), about 5%
    // higher here, far outside the sampling error of about 0.7%).
    constexpr int n = 2000;
    constexpr double mu_rate = 0.05;
    constexpr double sigma = 0.3;
    const rng::RngHub hub{rng::Seed{0x8888'0005ULL}};

    const Vector prices = Vector::Constant(n, 100.0);
    const Vector mu = Vector::Constant(n, mu_rate);
    const Matrix chol = sigma * Matrix::Identity(n, n);

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, n), Dynamics::Geometric};
    walk.advance(1.0);

    const double mean_ratio = walk.prices().mean() / 100.0;
    CHECK(mean_ratio == Catch::Approx(std::exp(mu_rate)).margin(0.02));
}
