/// \file
/// Tests for mm/avellaneda_stoikov_strategy.hpp: the two free functions on
/// their own (sigma_times_q against CovarianceMatrix, make_asset_quotes'
/// tick rounding and drop-below-one-tick rule), then the strategy itself,
/// anchored against math's own scalar N=1 reference formula
/// (optimal_total_spread), the AS6 cross-asset skew invariant (now safe to
/// exercise with N=2 since the Vector/Matrix bug is fixed), cold assets,
/// mid validation, and every constructor precondition.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/avellaneda_stoikov_strategy.hpp>
#include <mm/horizon.hpp>
#include <mm/quote.hpp>
#include <mm/quoting_strategy.hpp>
#include <mm/risk.hpp>
#include <mm/types.hpp>

#include <math/avellaneda_stoikov.hpp>
#include <math/covariance/covariance_matrix.hpp>
#include <math/stats/volatility.hpp>
#include <math/types.hpp>

namespace mm = mms::mm;
namespace math = mms::math;
namespace as = mms::math::avellaneda_stoikov;

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

/// A settings block for `assets` assets, all identical: gamma 2, k 1, no
/// correlation, a ConstantVolEstimator of sigma 0.2, a constant horizon of 1,
/// quote size 10, tick 0.01, position limit 1000, no portfolio cap.
mm::AvellanedaStoikovSettings default_settings(std::size_t assets) {
    std::vector<math::stats::AnyVolatilityEstimator> vols;
    for (std::size_t i = 0; i < assets; ++i) {
        vols.emplace_back(math::stats::ConstantVolEstimator{0.2});
    }
    return mm::AvellanedaStoikovSettings{
        .params = {.gamma = 2.0,
                   .k = math::Vector::Constant(static_cast<Eigen::Index>(assets), 1.0)},
        .correlation = identity(static_cast<Eigen::Index>(assets)),
        .volatility = std::move(vols),
        .horizon = mm::ConstantHorizon{.tau = 1.0},
        .quote_sizes = std::vector<mm::Quantity>(assets, 10),
        .tick_sizes = std::vector<mm::Price>(assets, 0.01),
        .limits = {.max_abs_position = std::vector<mm::Quantity>(assets, 1000),
                   .max_portfolio_risk = std::nullopt},
        .min_samples = 0};
}

} // namespace

TEST_CASE("sigma_times_q matches Sigma * q built from a CovarianceMatrix", "[mm][avellaneda]") {
    const math::Vector sigma = vec({0.2, 0.1, 0.3});
    math::Matrix correlation(3, 3);
    // clang-format off
    correlation << 1.0,  0.5, -0.2,
                  0.5,  1.0,  0.1,
                 -0.2,  0.1,  1.0;
    // clang-format on
    const math::Vector q = vec({5.0, -3.0, 2.0});

    const math::Vector actual = mm::sigma_times_q(sigma, correlation, q);

    const std::vector<double> vols{sigma(0), sigma(1), sigma(2)};
    const math::Matrix sigma_matrix =
        math::covariance::CovarianceMatrix::from_correlation(vols, correlation).matrix();
    const math::Vector expected = sigma_matrix * q;

    REQUIRE(actual.size() == 3);
    for (Eigen::Index i = 0; i < 3; ++i) {
        CHECK(actual(i) == Catch::Approx(expected(i)).margin(1e-9));
    }
}

TEST_CASE("sigma_times_q with no correlation is the diagonal case, sigma_i^2 * q_i",
          "[mm][avellaneda]") {
    const math::Vector actual = mm::sigma_times_q(vec({0.2, 0.5}), identity(2), vec({4.0, -2.0}));

    CHECK(actual(0) == Catch::Approx(0.04 * 4.0).margin(1e-12));
    CHECK(actual(1) == Catch::Approx(0.25 * -2.0).margin(1e-12));
}

TEST_CASE("sigma_times_q rejects mismatched sizes", "[mm][avellaneda]") {
    CHECK_THROWS_AS(mm::sigma_times_q(vec({0.1, 0.2}), identity(3), vec({1.0, 1.0})),
                    std::invalid_argument);
    CHECK_THROWS_AS(mm::sigma_times_q(vec({0.1, 0.2}), identity(2), vec({1.0})),
                    std::invalid_argument);
}

TEST_CASE("make_asset_quotes rounds the bid down and the ask up to the tick", "[mm][avellaneda]") {
    const mm::AssetQuotes quotes = mm::make_asset_quotes(100.0, 0.05, 0.01, 10);

    REQUIRE(quotes.bid.has_value());
    REQUIRE(quotes.ask.has_value());
    CHECK(quotes.bid->price == Catch::Approx(99.95).margin(1e-9));
    CHECK(quotes.ask->price == Catch::Approx(100.05).margin(1e-9));
    CHECK(quotes.bid->quantity == 10);
    CHECK(quotes.ask->quantity == 10);
}

TEST_CASE("make_asset_quotes drops the bid when it would land below one tick", "[mm][avellaneda]") {
    // reservation 0.005, half-spread 0.01: raw bid is negative.
    const mm::AssetQuotes quotes = mm::make_asset_quotes(0.005, 0.01, 0.01, 5);

    CHECK_FALSE(quotes.bid.has_value());
    REQUIRE(quotes.ask.has_value());
}

TEST_CASE("make_asset_quotes drops both sides when even the ask is below one tick",
          "[mm][avellaneda]") {
    const mm::AssetQuotes quotes = mm::make_asset_quotes(-1.0, 0.01, 0.01, 5);

    CHECK_FALSE(quotes.bid.has_value());
    CHECK_FALSE(quotes.ask.has_value());
}

TEST_CASE("make_asset_quotes rejects a non-positive size", "[mm][avellaneda]") {
    CHECK_THROWS_AS(mm::make_asset_quotes(100.0, 0.05, 0.01, 0), std::invalid_argument);
    CHECK_THROWS_AS(mm::make_asset_quotes(100.0, 0.05, 0.01, -1), std::invalid_argument);
}

TEST_CASE("A single flat asset reproduces math's own scalar reference spread", "[mm][avellaneda]") {
    // N=1: optimal_total_spread(gamma, sigma^2, tau, k) is the paper-anchored
    // total spread; the strategy's own half-spread, doubled, must match it.
    constexpr double gamma = 2.0;
    constexpr double sigma = 0.2;
    constexpr double tau = 1.0;
    constexpr double k = 1.0;

    mm::AvellanedaStoikovSettings settings = default_settings(1);
    settings.params.gamma = gamma;
    settings.params.k = vec({k});
    settings.volatility = {
        math::stats::AnyVolatilityEstimator{math::stats::ConstantVolEstimator{sigma}}};
    settings.horizon = mm::ConstantHorizon{.tau = tau};

    mm::AvellanedaStoikovStrategy strategy(std::move(settings));
    strategy.on_market_data(vec({100.0}), 1.0); // priming call

    const mm::QuoteSet quotes =
        strategy.quote(mm::MarketView{.mids = vec({100.0}), .inventory = vec({0.0}), .time = 0.0});

    REQUIRE(quotes.size() == 1);
    REQUIRE(quotes[0].bid.has_value());
    REQUIRE(quotes[0].ask.has_value());

    const double observed_spread = quotes[0].ask->price - quotes[0].bid->price;
    const double expected_spread = as::optimal_total_spread(gamma, sigma * sigma, tau, k);
    CHECK(observed_spread == Catch::Approx(expected_spread).margin(0.01)); // tick rounding

    // Flat, so the reservation price sits at the mid: the quotes are symmetric.
    const double mid = 100.0;
    CHECK((mid - quotes[0].bid->price) == Catch::Approx(quotes[0].ask->price - mid).margin(1e-9));
}

TEST_CASE("A long position skews the reservation price down, a short skews it up",
          "[mm][avellaneda]") {
    mm::AvellanedaStoikovStrategy strategy(default_settings(1));
    strategy.on_market_data(vec({100.0}), 1.0);

    const mm::QuoteSet flat =
        strategy.quote(mm::MarketView{.mids = vec({100.0}), .inventory = vec({0.0}), .time = 0.0});
    const mm::QuoteSet longed =
        strategy.quote(mm::MarketView{.mids = vec({100.0}), .inventory = vec({5.0}), .time = 0.0});
    const mm::QuoteSet shorted =
        strategy.quote(mm::MarketView{.mids = vec({100.0}), .inventory = vec({-5.0}), .time = 0.0});

    const double flat_mid = ((*flat[0].bid).price / 2.0) + ((*flat[0].ask).price / 2.0);
    const double long_mid = ((*longed[0].bid).price / 2.0) + ((*longed[0].ask).price / 2.0);
    const double short_mid = ((*shorted[0].bid).price / 2.0) + ((*shorted[0].ask).price / 2.0);

    CHECK(long_mid < flat_mid);
    CHECK(short_mid > flat_mid);
    CHECK(short_mid > long_mid);
}

TEST_CASE("The AS6 cross-asset invariant: a long position on one asset skews a positively "
          "correlated asset's reservation price down too",
          "[mm][avellaneda]") {
    mm::AvellanedaStoikovSettings settings = default_settings(2);
    math::Matrix correlation(2, 2);
    correlation << 1.0, 0.8, 0.8, 1.0;
    settings.correlation = correlation;

    mm::AvellanedaStoikovStrategy strategy(std::move(settings));
    strategy.on_market_data(vec({100.0, 50.0}), 1.0);

    const mm::QuoteSet flat = strategy.quote(
        mm::MarketView{.mids = vec({100.0, 50.0}), .inventory = vec({0.0, 0.0}), .time = 0.0});
    // Long asset 0 only; asset 1 is still flat in its OWN inventory.
    const mm::QuoteSet skewed = strategy.quote(
        mm::MarketView{.mids = vec({100.0, 50.0}), .inventory = vec({10.0, 0.0}), .time = 0.0});

    REQUIRE(flat[1].bid.has_value());
    REQUIRE(flat[1].ask.has_value());
    REQUIRE(skewed[1].bid.has_value());
    REQUIRE(skewed[1].ask.has_value());

    const double flat_mid_1 = ((*flat[1].bid).price / 2.0) + ((*flat[1].ask).price / 2.0);
    const double skewed_mid_1 = ((*skewed[1].bid).price / 2.0) + ((*skewed[1].ask).price / 2.0);

    CHECK(skewed_mid_1 < flat_mid_1); // sell-skew on asset 1, driven by asset 0's long
}

TEST_CASE("A cold asset (not enough samples yet) is not quoted", "[mm][avellaneda]") {
    mm::AvellanedaStoikovSettings settings = default_settings(1);
    settings.min_samples = 5;
    settings.volatility = {
        math::stats::AnyVolatilityEstimator{math::stats::EwmaVolEstimator{10.0}}};

    mm::AvellanedaStoikovStrategy strategy(std::move(settings));
    strategy.on_market_data(vec({100.0}), 1.0); // priming: no sample fed yet
    strategy.on_market_data(vec({100.1}), 1.0); // 1 sample: still short of min_samples

    const mm::QuoteSet quotes =
        strategy.quote(mm::MarketView{.mids = vec({100.1}), .inventory = vec({0.0}), .time = 0.0});

    REQUIRE(quotes.size() == 1);
    CHECK_FALSE(quotes[0].bid.has_value());
    CHECK_FALSE(quotes[0].ask.has_value());
}

TEST_CASE("A cold asset contributes no risk to a correlated warm asset", "[mm][avellaneda]") {
    mm::AvellanedaStoikovSettings settings = default_settings(2);
    math::Matrix correlation(2, 2);
    correlation << 1.0, 0.8, 0.8, 1.0;
    settings.correlation = correlation;
    // min_samples stays 0 (the default): asset 1 is cold not because of the
    // sample count, but because a fresh EwmaVolEstimator reports NaN before
    // its first real sample, and the priming call below feeds it none.
    settings.volatility[1] =
        math::stats::AnyVolatilityEstimator{math::stats::EwmaVolEstimator{10.0}};

    mm::AvellanedaStoikovStrategy strategy(std::move(settings));
    strategy.on_market_data(vec({100.0, 50.0}), 1.0);

    // Asset 1 is long 10 (would skew asset 0 if warm), but it is cold.
    const mm::QuoteSet quotes = strategy.quote(
        mm::MarketView{.mids = vec({100.0, 50.0}), .inventory = vec({0.0, 10.0}), .time = 0.0});
    const mm::QuoteSet baseline = strategy.quote(
        mm::MarketView{.mids = vec({100.0, 50.0}), .inventory = vec({0.0, 0.0}), .time = 0.0});

    REQUIRE(quotes[0].bid.has_value());
    REQUIRE(baseline[0].bid.has_value());
    CHECK(quotes[0].bid->price == baseline[0].bid->price);
    CHECK(quotes[0].ask->price == baseline[0].ask->price);
    CHECK_FALSE(quotes[1].bid.has_value()); // asset 1 itself stays unquoted
}

TEST_CASE("volatilities reports each estimator's current sigma", "[mm][avellaneda]") {
    const mm::AvellanedaStoikovStrategy strategy(default_settings(2));

    const math::Vector sigma = strategy.volatilities();

    REQUIRE(sigma.size() == 2);
    CHECK(sigma(0) == 0.2);
    CHECK(sigma(1) == 0.2);
}

TEST_CASE("do_on_market_data rejects a non-positive mid, leaving the estimators untouched",
          "[mm][avellaneda]") {
    mm::AvellanedaStoikovStrategy strategy(default_settings(1));
    strategy.on_market_data(vec({100.0}), 1.0);

    CHECK_THROWS_AS(strategy.on_market_data(vec({0.0}), 1.0), std::invalid_argument);
    CHECK_THROWS_AS(strategy.on_market_data(vec({-5.0}), 1.0), std::invalid_argument);

    const math::Vector before = strategy.volatilities();
    CHECK_THROWS_AS(strategy.on_market_data(vec({-1.0}), 1.0), std::invalid_argument);
    CHECK(strategy.volatilities()(0) == before(0));
}

TEST_CASE("The first on_market_data call only primes: it feeds the estimator nothing",
          "[mm][avellaneda]") {
    mm::AvellanedaStoikovSettings settings = default_settings(1);
    settings.volatility = {
        math::stats::AnyVolatilityEstimator{math::stats::EwmaVolEstimator{10.0}}};
    mm::AvellanedaStoikovStrategy strategy(std::move(settings));

    strategy.on_market_data(vec({100.0}), 1.0);

    CHECK(std::isnan(strategy.volatilities()(0))); // no sample fed yet
}

TEST_CASE("The constructor rejects mismatched vector sizes", "[mm][avellaneda]") {
    mm::AvellanedaStoikovSettings bad = default_settings(2);
    bad.quote_sizes.pop_back();
    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);

    bad = default_settings(2);
    bad.tick_sizes.pop_back();
    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);

    bad = default_settings(2);
    bad.volatility.pop_back();
    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);

    bad = default_settings(2);
    bad.limits.max_abs_position.pop_back();
    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);
}

TEST_CASE("The constructor rejects a correlation matrix of the wrong size", "[mm][avellaneda]") {
    mm::AvellanedaStoikovSettings bad = default_settings(2);
    bad.correlation = identity(3);

    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);
}

TEST_CASE("The constructor rejects an invalid gamma or k", "[mm][avellaneda]") {
    mm::AvellanedaStoikovSettings bad = default_settings(1);
    bad.params.gamma = 0.0;
    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);

    bad = default_settings(1);
    bad.params.k = vec({0.0});
    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);
}

TEST_CASE("The constructor rejects a non-positive quote size or tick", "[mm][avellaneda]") {
    mm::AvellanedaStoikovSettings bad = default_settings(1);
    bad.quote_sizes = {0};
    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);

    bad = default_settings(1);
    bad.tick_sizes = {0.0};
    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);
}

TEST_CASE("The constructor rejects an invalid horizon", "[mm][avellaneda]") {
    mm::AvellanedaStoikovSettings bad = default_settings(1);
    bad.horizon = mm::ConstantHorizon{.tau = 0.0};

    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);
}

TEST_CASE("The constructor rejects invalid risk limits", "[mm][avellaneda]") {
    mm::AvellanedaStoikovSettings bad = default_settings(1);
    bad.limits.max_abs_position = {0};

    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);
}

TEST_CASE("The constructor rejects a correlation matrix that is not positive definite",
          "[mm][avellaneda]") {
    mm::AvellanedaStoikovSettings bad = default_settings(2);
    math::Matrix correlation(2, 2);
    correlation << 1.0, 1.5, 1.5, 1.0; // not a valid correlation matrix
    bad.correlation = correlation;

    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);
}

TEST_CASE("The constructor rejects zero assets", "[mm][avellaneda]") {
    const mm::AvellanedaStoikovSettings bad = default_settings(0);

    CHECK_THROWS_AS(mm::AvellanedaStoikovStrategy(bad), std::invalid_argument);
}

TEST_CASE("The strategy is usable through the QuotingStrategy base", "[mm][avellaneda]") {
    const std::unique_ptr<mm::QuotingStrategy> strategy =
        std::make_unique<mm::AvellanedaStoikovStrategy>(default_settings(1));

    strategy->on_market_data(vec({100.0}), 1.0);
    const mm::QuoteSet quotes =
        strategy->quote(mm::MarketView{.mids = vec({100.0}), .inventory = vec({0.0}), .time = 0.0});

    CHECK(quotes.size() == 1);
}
