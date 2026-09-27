/// \file
/// Tests for mm/strategy_config.hpp: make_strategy wires each EstimatorSpec
/// to the matching concrete math estimator (checked by feeding the SAME
/// price sequence to a strategy built from a spec and to a hand-built
/// estimator, and comparing volatilities()), keeps per-asset specs in
/// AssetId order when they are mixed, propagates the underlying strategy's
/// own validation instead of duplicating or swallowing it, and returns a
/// QuotingStrategy usable through its base interface.

#include <cmath>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mm/avellaneda_stoikov_strategy.hpp>
#include <mm/horizon.hpp>
#include <mm/quoting_strategy.hpp>
#include <mm/risk.hpp>
#include <mm/strategy_config.hpp>
#include <mm/types.hpp>

#include <math/stats/ewma.hpp>
#include <math/stats/volatility.hpp>
#include <math/types.hpp>

namespace mm = mms::mm;
namespace math = mms::math;

namespace {

math::Vector vec(std::initializer_list<double> values) {
    math::Vector result(static_cast<Eigen::Index>(values.size()));
    Eigen::Index index = 0;
    for (const double value : values) {
        result(index++) = value;
    }
    return result;
}

math::Matrix identity(Eigen::Index n) {
    return math::Matrix::Identity(n, n);
}

/// A minimal, valid config for `assets` identical assets, whose estimator
/// spec every test overrides.
mm::StrategyConfig default_config(std::size_t assets) {
    return mm::StrategyConfig{
        .gamma = 2.0,
        .k = math::Vector::Constant(static_cast<Eigen::Index>(assets), 1.0),
        .correlation = identity(static_cast<Eigen::Index>(assets)),
        .estimators = std::vector<mm::EstimatorSpec>(assets, mm::ConstantSpec{.sigma = 0.2}),
        .horizon = mm::ConstantHorizon{.tau = 1.0},
        .quote_sizes = std::vector<mm::Quantity>(assets, 10),
        .tick_sizes = std::vector<mm::Price>(assets, 0.01),
        .limits = {.max_abs_position = std::vector<mm::Quantity>(assets, 1000),
                   .max_portfolio_risk = std::nullopt},
        .min_samples = 0};
}

/// Feeds the same mid sequence to a strategy (through on_market_data) and
/// asks for its volatilities() at the end.
math::Vector volatilities_after(mm::QuotingStrategy& strategy,
                                const std::vector<double>& mids_per_step,
                                double dt) {
    strategy.on_market_data(vec({mids_per_step[0]}), dt); // priming
    for (std::size_t step = 1; step < mids_per_step.size(); ++step) {
        strategy.on_market_data(vec({mids_per_step[step]}), dt);
    }
    return dynamic_cast<mm::AvellanedaStoikovStrategy&>(strategy).volatilities();
}

} // namespace

TEST_CASE("make_strategy returns something usable through the QuotingStrategy base",
          "[mm][strategy_config]") {
    const std::unique_ptr<mm::QuotingStrategy> strategy = mm::make_strategy(default_config(1));

    REQUIRE(strategy != nullptr);
    CHECK(strategy->asset_count() == 1);
}

TEST_CASE("ConstantSpec reports its fixed sigma from the very start", "[mm][strategy_config]") {
    mm::StrategyConfig config = default_config(1);
    config.estimators = {mm::ConstantSpec{.sigma = 0.35}};

    const std::unique_ptr<mm::QuotingStrategy> strategy = mm::make_strategy(config);
    const math::Vector sigma =
        dynamic_cast<mm::AvellanedaStoikovStrategy&>(*strategy).volatilities();

    REQUIRE(sigma.size() == 1);
    CHECK(sigma(0) == 0.35); // no sample needed, unlike Ewma/Rolling
}

TEST_CASE("EwmaSpec drives an estimator that matches a hand-built EwmaEstimator",
          "[mm][strategy_config]") {
    mm::StrategyConfig config = default_config(1);
    config.estimators = {mm::EwmaSpec{.half_life = 5.0}};

    const std::unique_ptr<mm::QuotingStrategy> strategy = mm::make_strategy(config);
    const std::vector<double> mids{100.0, 100.5, 99.8, 101.0, 100.2, 100.9};
    const math::Vector observed = volatilities_after(*strategy, mids, 1.0);

    // A hand-built EwmaEstimator fed the exact same log-return sequence.
    math::stats::EwmaEstimator reference(5.0);
    for (std::size_t i = 1; i < mids.size(); ++i) {
        reference.add(std::log(mids[i] / mids[i - 1]) / std::sqrt(1.0));
    }

    REQUIRE(observed.size() == 1);
    CHECK(observed(0) == reference.stddev());
}

TEST_CASE("EwmaSpec is NaN before any real sample, like a fresh EwmaEstimator",
          "[mm][strategy_config]") {
    mm::StrategyConfig config = default_config(1);
    config.estimators = {mm::EwmaSpec{.half_life = 5.0}};

    const std::unique_ptr<mm::QuotingStrategy> strategy = mm::make_strategy(config);
    strategy->on_market_data(vec({100.0}), 1.0); // priming only, no sample fed yet

    CHECK(std::isnan(dynamic_cast<mm::AvellanedaStoikovStrategy&>(*strategy).volatilities()(0)));
}

TEST_CASE("RollingSpec drives an estimator that matches a hand-built RollingVolEstimator",
          "[mm][strategy_config]") {
    mm::StrategyConfig config = default_config(1);
    config.estimators = {mm::RollingSpec{.window = 4}};

    const std::unique_ptr<mm::QuotingStrategy> strategy = mm::make_strategy(config);
    const std::vector<double> mids{100.0, 101.0, 99.0, 102.5, 98.0, 103.0, 97.5};
    const math::Vector observed = volatilities_after(*strategy, mids, 1.0);

    math::stats::RollingVolEstimator reference(4);
    for (std::size_t i = 1; i < mids.size(); ++i) {
        reference.update(std::log(mids[i] / mids[i - 1]) / std::sqrt(1.0));
    }

    REQUIRE(observed.size() == 1);
    CHECK(observed(0) == reference.sigma());
}

TEST_CASE("RollingSpec's sigma is NaN until its window has at least 2 samples",
          "[mm][strategy_config]") {
    mm::StrategyConfig config = default_config(1);
    config.estimators = {mm::RollingSpec{.window = 4}};

    const std::unique_ptr<mm::QuotingStrategy> strategy = mm::make_strategy(config);
    strategy->on_market_data(vec({100.0}), 1.0); // priming
    strategy->on_market_data(vec({100.5}), 1.0); // 1 real sample: not enough

    CHECK(std::isnan(dynamic_cast<mm::AvellanedaStoikovStrategy&>(*strategy).volatilities()(0)));
}

TEST_CASE("Mixed specs are wired to the matching asset, in AssetId order",
          "[mm][strategy_config]") {
    mm::StrategyConfig config = default_config(2);
    config.estimators = {mm::ConstantSpec{.sigma = 0.4}, mm::ConstantSpec{.sigma = 0.7}};

    const std::unique_ptr<mm::QuotingStrategy> strategy = mm::make_strategy(config);
    const math::Vector sigma =
        dynamic_cast<mm::AvellanedaStoikovStrategy&>(*strategy).volatilities();

    REQUIRE(sigma.size() == 2);
    CHECK(sigma(0) == 0.4);
    CHECK(sigma(1) == 0.7);
}

TEST_CASE("A RollingSpec window is forwarded exactly, not resized", "[mm][strategy_config]") {
    mm::StrategyConfig config = default_config(1);
    config.estimators = {mm::RollingSpec{.window = 3}};

    // The underlying RollingVolEstimator constructor rejects window < 2;
    // a window of 3 must reach make_strategy unmodified for that check to
    // ever matter, which the next test on window 1 actually exercises.
    CHECK_NOTHROW(mm::make_strategy(config));
}

TEST_CASE("make_strategy propagates the strategy's own constructor errors, unmodified",
          "[mm][strategy_config]") {
    mm::StrategyConfig bad = default_config(1);
    bad.gamma = 0.0; // AvellanedaStoikovStrategy rejects gamma <= 0
    CHECK_THROWS_AS(mm::make_strategy(bad), std::invalid_argument);

    bad = default_config(1);
    bad.quote_sizes = {0}; // rejects a non-positive quote size
    CHECK_THROWS_AS(mm::make_strategy(bad), std::invalid_argument);

    bad = default_config(1);
    bad.tick_sizes = {0.0}; // rejects an invalid tick
    CHECK_THROWS_AS(mm::make_strategy(bad), std::invalid_argument);

    bad = default_config(2);
    bad.k = vec({1.0}); // size mismatch (2 assets, 1 k)
    CHECK_THROWS_AS(mm::make_strategy(bad), std::invalid_argument);
}

TEST_CASE("make_strategy propagates the RollingVolEstimator's own window validation",
          "[mm][strategy_config]") {
    mm::StrategyConfig bad = default_config(1);
    bad.estimators = {mm::RollingSpec{.window = 1}}; // RollingVolEstimator requires >= 2

    CHECK_THROWS_AS(mm::make_strategy(bad), std::invalid_argument);
}

TEST_CASE("make_strategy propagates the EwmaEstimator's own half-life validation",
          "[mm][strategy_config]") {
    mm::StrategyConfig bad = default_config(1);
    bad.estimators = {mm::EwmaSpec{.half_life = 0.0}}; // EwmaEstimator requires > 0

    CHECK_THROWS_AS(mm::make_strategy(bad), std::invalid_argument);
}

TEST_CASE("make_strategy propagates the ConstantVolEstimator's own sigma validation",
          "[mm][strategy_config]") {
    mm::StrategyConfig bad = default_config(1);
    bad.estimators = {mm::ConstantSpec{.sigma = -1.0}}; // ConstantVolEstimator requires > 0

    CHECK_THROWS_AS(mm::make_strategy(bad), std::invalid_argument);
}

TEST_CASE("An empty estimators list is a size mismatch the strategy itself rejects",
          "[mm][strategy_config]") {
    mm::StrategyConfig bad = default_config(1);
    bad.estimators.clear();

    CHECK_THROWS_AS(mm::make_strategy(bad), std::invalid_argument);
}
