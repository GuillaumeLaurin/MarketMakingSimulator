/// \file
/// Tests for FactorModel. advance() is cross-checked against
/// mu*dt + beta*f + idio_vol.cwiseProduct(idio_z)*sqrt(dt) computed
/// independently over copies of both engine groups, over several successive
/// ticks (including one asset with idio_vol == 0, a legitimate case).
/// Construction-time validation (asset dims, factor dims, idio_vol contract),
/// reset()'s restoration of both engine groups, advance(0.0), and the
/// PriceProcess dt >= 0 contract are also covered.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/dist/normal.hpp>
#include <math/process/factor_model.hpp>
#include <math/process/increment.hpp>
#include <math/rng/stream.hpp>

namespace {

using mms::math::Index;
using mms::math::Matrix;
using mms::math::Vector;
using mms::math::process::correlated_normal_increment;
using mms::math::process::Dynamics;
using mms::math::process::FactorModel;
namespace rng = mms::math::rng;

std::vector<rng::RngEngine> engines_for(const rng::RngHub& hub, rng::Subsystem subsystem, int n) {
    std::vector<rng::RngEngine> engines;
    for (int i = 0; i < n; ++i) {
        engines.push_back(hub.engine_for(
            {.subsystem = subsystem, .asset = rng::AssetKey{static_cast<std::uint64_t>(i)}}));
    }
    return engines;
}

double max_abs_diff(const Vector& a, const Vector& b) {
    return (a - b).cwiseAbs().maxCoeff();
}

/// A valid 3-asset, 2-factor scenario, reused across tests. Asset 1's
/// idio_vol is 0: its variance is fully explained by the factors.
struct Scenario {
    Vector prices;
    Vector mu;
    Matrix beta;
    Matrix factor_chol;
    Vector idio_vol;

    static Scenario make() {
        Scenario s;
        s.prices.resize(3);
        s.prices << 100.0, 50.0, 25.0;
        s.mu.resize(3);
        s.mu << 0.01, -0.02, 0.03;
        s.beta.resize(3, 2);
        s.beta << 1.0, 0.5, 0.2, 1.0, 0.0, 0.3;
        s.factor_chol.resize(2, 2);
        s.factor_chol << 1.0, 0.0, 0.4, 0.9;
        s.idio_vol.resize(3);
        s.idio_vol << 0.1, 0.0, 0.2;
        return s;
    }
};

} // namespace

TEST_CASE("FactorModel rejects inconsistent asset-count dimensions", "[process][factor_model]") {
    const rng::RngHub hub{rng::Seed{0xA111'A111ULL}};
    const Scenario s = Scenario::make();

    CHECK_THROWS_AS(FactorModel(s.prices,
                                Vector(4),
                                s.beta,
                                s.factor_chol,
                                s.idio_vol,
                                engines_for(hub, rng::Subsystem::Price, 3),
                                engines_for(hub, rng::Subsystem::PriceFactor, 2),
                                Dynamics::Arithmetic),
                    std::invalid_argument); // mu wrong size

    CHECK_THROWS_AS(FactorModel(s.prices,
                                s.mu,
                                Matrix(4, 2),
                                s.factor_chol,
                                s.idio_vol,
                                engines_for(hub, rng::Subsystem::Price, 3),
                                engines_for(hub, rng::Subsystem::PriceFactor, 2),
                                Dynamics::Arithmetic),
                    std::invalid_argument); // beta wrong row count

    CHECK_THROWS_AS(FactorModel(s.prices,
                                s.mu,
                                s.beta,
                                s.factor_chol,
                                Vector(4),
                                engines_for(hub, rng::Subsystem::Price, 3),
                                engines_for(hub, rng::Subsystem::PriceFactor, 2),
                                Dynamics::Arithmetic),
                    std::invalid_argument); // idio_vol wrong size

    CHECK_THROWS_AS(FactorModel(s.prices,
                                s.mu,
                                s.beta,
                                s.factor_chol,
                                s.idio_vol,
                                engines_for(hub, rng::Subsystem::Price, 2),
                                engines_for(hub, rng::Subsystem::PriceFactor, 2),
                                Dynamics::Arithmetic),
                    std::invalid_argument); // wrong number of asset engines
}

TEST_CASE("FactorModel rejects inconsistent factor-count dimensions", "[process][factor_model]") {
    const rng::RngHub hub{rng::Seed{0xA222'A222ULL}};
    const Scenario s = Scenario::make();

    CHECK_THROWS_AS(FactorModel(s.prices,
                                s.mu,
                                s.beta,
                                Matrix(3, 3),
                                s.idio_vol,
                                engines_for(hub, rng::Subsystem::Price, 3),
                                engines_for(hub, rng::Subsystem::PriceFactor, 2),
                                Dynamics::Arithmetic),
                    std::invalid_argument); // factor_chol wrong size

    CHECK_THROWS_AS(FactorModel(s.prices,
                                s.mu,
                                s.beta,
                                s.factor_chol,
                                s.idio_vol,
                                engines_for(hub, rng::Subsystem::Price, 3),
                                engines_for(hub, rng::Subsystem::PriceFactor, 3),
                                Dynamics::Arithmetic),
                    std::invalid_argument); // wrong number of factor engines
}

TEST_CASE("FactorModel rejects a non-finite or negative idio_vol entry",
          "[process][factor_model]") {
    const rng::RngHub hub{rng::Seed{0xA333'A333ULL}};
    const Scenario s = Scenario::make();

    Vector negative_idio = s.idio_vol;
    negative_idio(0) = -0.5;
    CHECK_THROWS_AS(FactorModel(s.prices,
                                s.mu,
                                s.beta,
                                s.factor_chol,
                                negative_idio,
                                engines_for(hub, rng::Subsystem::Price, 3),
                                engines_for(hub, rng::Subsystem::PriceFactor, 2),
                                Dynamics::Arithmetic),
                    std::invalid_argument);

    Vector nan_idio = s.idio_vol;
    nan_idio(2) = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(FactorModel(s.prices,
                                s.mu,
                                s.beta,
                                s.factor_chol,
                                nan_idio,
                                engines_for(hub, rng::Subsystem::Price, 3),
                                engines_for(hub, rng::Subsystem::PriceFactor, 2),
                                Dynamics::Arithmetic),
                    std::invalid_argument);

    // idio_vol == 0 for one asset is legitimate: fully factor-explained variance.
    CHECK_NOTHROW(FactorModel(s.prices,
                              s.mu,
                              s.beta,
                              s.factor_chol,
                              s.idio_vol,
                              engines_for(hub, rng::Subsystem::Price, 3),
                              engines_for(hub, rng::Subsystem::PriceFactor, 2),
                              Dynamics::Arithmetic));
}

TEST_CASE("FactorModel exposes the initial prices and asset count before any advance",
          "[process][factor_model]") {
    const rng::RngHub hub{rng::Seed{0xA444'A444ULL}};
    const Scenario s = Scenario::make();

    FactorModel model{s.prices,
                      s.mu,
                      s.beta,
                      s.factor_chol,
                      s.idio_vol,
                      engines_for(hub, rng::Subsystem::Price, 3),
                      engines_for(hub, rng::Subsystem::PriceFactor, 2),
                      Dynamics::Arithmetic};

    CHECK(model.asset_count() == 3);
    CHECK(max_abs_diff(model.prices(), s.prices) < 1e-15);
}

TEST_CASE("FactorModel::advance matches the two-draw decomposition over several ticks",
          "[process][factor_model]") {
    const rng::RngHub hub{rng::Seed{0xA555'A555ULL}};
    const Scenario s = Scenario::make();

    std::vector<rng::RngEngine> engines = engines_for(hub, rng::Subsystem::Price, 3);
    std::vector<rng::RngEngine> factor_engines = engines_for(hub, rng::Subsystem::PriceFactor, 2);
    std::vector<rng::RngEngine> ref_engines = engines;
    std::vector<rng::RngEngine> ref_factor_engines = factor_engines;

    constexpr double dt = 0.1;
    FactorModel model{s.prices,
                      s.mu,
                      s.beta,
                      s.factor_chol,
                      s.idio_vol,
                      std::move(engines),
                      std::move(factor_engines),
                      Dynamics::Arithmetic};

    Vector expected = s.prices;
    for (int tick = 0; tick < 5; ++tick) {
        model.advance(dt);

        const Vector f = correlated_normal_increment(s.factor_chol, ref_factor_engines, dt);
        Vector idio_z(3);
        for (Index i = 0; i < 3; ++i) {
            idio_z(i) = mms::math::dist::standard_normal(ref_engines[static_cast<std::size_t>(i)]);
        }
        expected += (s.mu * dt) + (s.beta * f) + (s.idio_vol.cwiseProduct(idio_z) * std::sqrt(dt));

        CHECK(max_abs_diff(model.prices(), expected) < 1e-12);
    }
}

TEST_CASE("FactorModel::reset restores prices and both engine groups", "[process][factor_model]") {
    const rng::RngHub hub{rng::Seed{0xA666'A666ULL}};
    const Scenario s = Scenario::make();

    FactorModel model{s.prices,
                      s.mu,
                      s.beta,
                      s.factor_chol,
                      s.idio_vol,
                      engines_for(hub, rng::Subsystem::Price, 3),
                      engines_for(hub, rng::Subsystem::PriceFactor, 2),
                      Dynamics::Arithmetic};

    model.advance(0.5);
    model.advance(0.5);
    const Vector after_first_run = model.prices();

    model.reset();
    CHECK(max_abs_diff(model.prices(), s.prices) < 1e-15);

    model.advance(0.5);
    model.advance(0.5);
    CHECK(max_abs_diff(model.prices(), after_first_run) < 1e-12);
}

TEST_CASE("FactorModel::advance(0.0) leaves prices unchanged", "[process][factor_model]") {
    const rng::RngHub hub{rng::Seed{0xA777'A777ULL}};
    const Scenario s = Scenario::make();

    FactorModel model{s.prices,
                      s.mu,
                      s.beta,
                      s.factor_chol,
                      s.idio_vol,
                      engines_for(hub, rng::Subsystem::Price, 3),
                      engines_for(hub, rng::Subsystem::PriceFactor, 2),
                      Dynamics::Arithmetic};
    model.advance(0.0);

    CHECK(max_abs_diff(model.prices(), s.prices) < 1e-15);
}

TEST_CASE("FactorModel::advance rejects a negative dt via the PriceProcess contract",
          "[process][factor_model]") {
    const rng::RngHub hub{rng::Seed{0xA888'A888ULL}};
    const Scenario s = Scenario::make();

    FactorModel model{s.prices,
                      s.mu,
                      s.beta,
                      s.factor_chol,
                      s.idio_vol,
                      engines_for(hub, rng::Subsystem::Price, 3),
                      engines_for(hub, rng::Subsystem::PriceFactor, 2),
                      Dynamics::Arithmetic};

    CHECK_THROWS_AS(model.advance(-1.0), std::invalid_argument);
    CHECK(max_abs_diff(model.prices(), s.prices) < 1e-15);
}

TEST_CASE("FactorModel with beta == 0 and factor_chol == 0 is pure deterministic drift plus "
          "idiosyncratic noise",
          "[process][factor_model]") {
    const rng::RngHub hub{rng::Seed{0xA999'A999ULL}};
    Vector prices(2);
    prices << 100.0, 100.0;
    Vector mu(2);
    mu << 1.0, -2.0;
    const Matrix beta = Matrix::Zero(2, 1);
    const Matrix factor_chol = Matrix::Zero(1, 1);
    const Vector idio_vol = Vector::Zero(2); // and idio_vol == 0: no noise at all

    std::vector<rng::RngEngine> engines = engines_for(hub, rng::Subsystem::Price, 2);
    std::vector<rng::RngEngine> factor_engines = engines_for(hub, rng::Subsystem::PriceFactor, 1);

    FactorModel model{prices,
                      mu,
                      beta,
                      factor_chol,
                      idio_vol,
                      std::move(engines),
                      std::move(factor_engines),
                      Dynamics::Arithmetic};
    model.advance(2.0);

    CHECK(model.prices()(0) == Catch::Approx(102.0));
    CHECK(model.prices()(1) == Catch::Approx(96.0));
}

TEST_CASE("FactorModel with geometric dynamics rejects initial prices that are not finite and "
          "positive",
          "[process][factor_model][geometric]") {
    const rng::RngHub hub{rng::Seed{0xB111'B111ULL}};
    const Scenario s = Scenario::make();

    for (const double bad : {0.0,
                             -1.0,
                             std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        Vector prices = s.prices;
        prices(1) = bad;
        CHECK_THROWS_AS(FactorModel(prices,
                                    s.mu,
                                    s.beta,
                                    s.factor_chol,
                                    s.idio_vol,
                                    engines_for(hub, rng::Subsystem::Price, 3),
                                    engines_for(hub, rng::Subsystem::PriceFactor, 2),
                                    Dynamics::Geometric),
                        std::invalid_argument);
        if (std::isfinite(bad)) {
            CHECK_NOTHROW(FactorModel(prices,
                                      s.mu,
                                      s.beta,
                                      s.factor_chol,
                                      s.idio_vol,
                                      engines_for(hub, rng::Subsystem::Price, 3),
                                      engines_for(hub, rng::Subsystem::PriceFactor, 2),
                                      Dynamics::Arithmetic));
        }
    }
}

TEST_CASE("FactorModel geometric advance matches exp((mu - variance/2) dt + noise) with the "
          "variance of beta L L^T beta^T + idio^2",
          "[process][factor_model][geometric]") {
    const rng::RngHub hub{rng::Seed{0xB222'B222ULL}};
    const Scenario s = Scenario::make();

    std::vector<rng::RngEngine> engines = engines_for(hub, rng::Subsystem::Price, 3);
    std::vector<rng::RngEngine> factor_engines = engines_for(hub, rng::Subsystem::PriceFactor, 2);
    std::vector<rng::RngEngine> ref_engines = engines;
    std::vector<rng::RngEngine> ref_factor_engines = factor_engines;

    // The full asset covariance; only its diagonal is the variance.
    const Matrix factor_cov = s.factor_chol * s.factor_chol.transpose();
    const Matrix asset_cov =
        (s.beta * factor_cov * s.beta.transpose()) + Matrix(s.idio_vol.cwiseAbs2().asDiagonal());
    const Vector variance = asset_cov.diagonal();

    constexpr double dt = 0.1;
    FactorModel model{s.prices,
                      s.mu,
                      s.beta,
                      s.factor_chol,
                      s.idio_vol,
                      std::move(engines),
                      std::move(factor_engines),
                      Dynamics::Geometric};

    Vector expected = s.prices;
    for (int tick = 0; tick < 5; ++tick) {
        model.advance(dt);

        const Vector f = correlated_normal_increment(s.factor_chol, ref_factor_engines, dt);
        Vector idio_z(3);
        for (Index i = 0; i < 3; ++i) {
            idio_z(i) = mms::math::dist::standard_normal(ref_engines[static_cast<std::size_t>(i)]);
        }
        const Vector log_return = ((s.mu - 0.5 * variance) * dt) + (s.beta * f) +
                                  (s.idio_vol.cwiseProduct(idio_z) * std::sqrt(dt));
        expected = expected.cwiseProduct(log_return.array().exp().matrix());

        CHECK(max_abs_diff(model.prices(), expected) < 1e-12);
    }
}

TEST_CASE("FactorModel geometric with no noise grows at exp(mu dt)",
          "[process][factor_model][geometric]") {
    const rng::RngHub hub{rng::Seed{0xB333'B333ULL}};
    Vector prices(2);
    prices << 100.0, 100.0;
    Vector mu(2);
    mu << 0.5, -0.5;

    FactorModel model{prices,
                      mu,
                      Matrix::Zero(2, 1),
                      Matrix::Zero(1, 1),
                      Vector::Zero(2),
                      engines_for(hub, rng::Subsystem::Price, 2),
                      engines_for(hub, rng::Subsystem::PriceFactor, 1),
                      Dynamics::Geometric};
    model.advance(2.0);

    CHECK(model.prices()(0) == Catch::Approx(100.0 * std::exp(1.0)));
    CHECK(model.prices()(1) == Catch::Approx(100.0 * std::exp(-1.0)));
}

TEST_CASE("FactorModel exposes the factors' draw of the last step, zero before any step",
          "[process][factor_model][factors]") {
    const rng::RngHub hub{rng::Seed{0xC111'C111ULL}};
    const Scenario s = Scenario::make();
    std::vector<rng::RngEngine> factor_engines = engines_for(hub, rng::Subsystem::PriceFactor, 2);
    std::vector<rng::RngEngine> reference = factor_engines;
    FactorModel model{s.prices,
                      s.mu,
                      s.beta,
                      s.factor_chol,
                      s.idio_vol,
                      engines_for(hub, rng::Subsystem::Price, 3),
                      std::move(factor_engines),
                      Dynamics::Geometric};

    REQUIRE(model.factor_increments().size() == 2);
    CHECK(model.factor_increments().isZero(0.0));

    constexpr double dt = 0.25;
    for (int tick = 0; tick < 4; ++tick) {
        model.advance(dt);
        const Vector expected = correlated_normal_increment(s.factor_chol, reference, dt);

        REQUIRE(model.factor_increments().size() == 2);
        CHECK(max_abs_diff(model.factor_increments(), expected) < 1e-15);
        CHECK(model.factor_increments().norm() > 0.0);
    }
}

TEST_CASE("FactorModel's factor draw is the one its prices moved with, whatever the dynamics",
          "[process][factor_model][factors]") {
    const rng::RngHub hub{rng::Seed{0xC222'C222ULL}};
    const Scenario s = Scenario::make();
    const auto make = [&](Dynamics dynamics) {
        return FactorModel{s.prices,
                           s.mu,
                           s.beta,
                           s.factor_chol,
                           s.idio_vol,
                           engines_for(hub, rng::Subsystem::Price, 3),
                           engines_for(hub, rng::Subsystem::PriceFactor, 2),
                           dynamics};
    };
    FactorModel arithmetic = make(Dynamics::Arithmetic);
    FactorModel geometric = make(Dynamics::Geometric);

    arithmetic.advance(0.5);
    geometric.advance(0.5);

    // Same engines, same draws: the dynamics only change the update formula.
    CHECK(max_abs_diff(arithmetic.factor_increments(), geometric.factor_increments()) < 1e-15);
}

TEST_CASE("FactorModel::reset zeroes the factors and replays the same draws",
          "[process][factor_model][factors]") {
    const rng::RngHub hub{rng::Seed{0xC333'C333ULL}};
    const Scenario s = Scenario::make();
    FactorModel model{s.prices,
                      s.mu,
                      s.beta,
                      s.factor_chol,
                      s.idio_vol,
                      engines_for(hub, rng::Subsystem::Price, 3),
                      engines_for(hub, rng::Subsystem::PriceFactor, 2),
                      Dynamics::Arithmetic};
    model.advance(1.0);
    const Vector first_step = model.factor_increments();
    model.advance(1.0);

    model.reset();
    CHECK(model.factor_increments().isZero(0.0));
    model.advance(1.0);

    CHECK(max_abs_diff(model.factor_increments(), first_step) < 1e-15);
}

TEST_CASE("FactorModel::advance(0) draws factors of zero size",
          "[process][factor_model][factors]") {
    const rng::RngHub hub{rng::Seed{0xC444'C444ULL}};
    const Scenario s = Scenario::make();
    FactorModel model{s.prices,
                      s.mu,
                      s.beta,
                      s.factor_chol,
                      s.idio_vol,
                      engines_for(hub, rng::Subsystem::Price, 3),
                      engines_for(hub, rng::Subsystem::PriceFactor, 2),
                      Dynamics::Arithmetic};

    model.advance(0.0);

    REQUIRE(model.factor_increments().size() == 2);
    CHECK(model.factor_increments().isZero(0.0));
}

TEST_CASE("A PriceProcess reaches the factors through its base class",
          "[process][factor_model][factors]") {
    const rng::RngHub hub{rng::Seed{0xC555'C555ULL}};
    const Scenario s = Scenario::make();
    FactorModel model{s.prices,
                      s.mu,
                      s.beta,
                      s.factor_chol,
                      s.idio_vol,
                      engines_for(hub, rng::Subsystem::Price, 3),
                      engines_for(hub, rng::Subsystem::PriceFactor, 2),
                      Dynamics::Arithmetic};
    mms::math::process::PriceProcess& process = model;

    process.advance(1.0);

    CHECK(process.factor_increments().size() == 2);
    CHECK(&process.factor_increments() == &model.factor_increments());
}
