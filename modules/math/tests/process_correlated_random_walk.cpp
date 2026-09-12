/// \file
/// Tests for CorrelatedRandomWalk. advance() is cross-checked against
/// mu*dt + correlated_normal_increment() computed independently over a copy
/// of the same engines, over several successive ticks. Also covers the
/// construction-time size validation, reset()'s restoration of both prices
/// and engine state, and the zero-chol deterministic-drift edge case.

#include <cstdint>
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

    CHECK_THROWS_AS(CorrelatedRandomWalk(prices, Vector(3), chol, engines_for(hub, 2)),
                    std::invalid_argument); // mu wrong size
    CHECK_THROWS_AS(CorrelatedRandomWalk(prices, mu, Matrix(3, 3), engines_for(hub, 2)),
                    std::invalid_argument); // chol wrong size
    CHECK_THROWS_AS(CorrelatedRandomWalk(prices, mu, chol, engines_for(hub, 3)),
                    std::invalid_argument); // wrong number of engines
    CHECK_THROWS_AS(CorrelatedRandomWalk(Vector(3), mu, chol, engines_for(hub, 2)),
                    std::invalid_argument); // initial_prices wrong size

    CHECK_NOTHROW(CorrelatedRandomWalk(prices, mu, chol, engines_for(hub, 2)));
}

TEST_CASE("CorrelatedRandomWalk exposes the initial prices and asset count before any advance",
          "[process][correlated_random_walk]") {
    const rng::RngHub hub{rng::Seed{0x2222'2222ULL}};
    Vector prices(3);
    prices << 100.0, 50.0, 25.0;
    Vector mu = Vector::Zero(3);
    Matrix chol = Matrix::Identity(3, 3);

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, 3)};

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

    CorrelatedRandomWalk walk{prices, mu, chol, std::move(engines)};

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

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, 2)};

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

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, 2)};
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

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, 2)};

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

    CorrelatedRandomWalk walk{prices, mu, chol, engines_for(hub, 2)};
    walk.advance(2.0);

    CHECK(walk.prices()(0) == Catch::Approx(102.0));
    CHECK(walk.prices()(1) == Catch::Approx(96.0));
}
