/// \file
/// Implementation of AvellanedaStoikovStrategy (<mm/avellaneda_stoikov_strategy.hpp>).

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <mm/avellaneda_stoikov_strategy.hpp>
#include <mm/tick.hpp>

#include <math/covariance/covariance_matrix.hpp>

namespace mms::mm {

namespace {

[[noreturn]] void reject(const char* message) {
    throw std::invalid_argument(std::string("AvellanedaStoikovStrategy: ") + message);
}

} // namespace

math::Vector
sigma_times_q(const math::Vector& sigma, const math::Matrix& correlation, const math::Vector& q) {
    const Eigen::Index assets = sigma.size();
    if (q.size() != assets || correlation.rows() != assets || correlation.cols() != assets) {
        throw std::invalid_argument(
            "sigma_times_q: sigma, correlation and q must have matching sizes");
    }
    return sigma.cwiseProduct(correlation * sigma.cwiseProduct(q));
}

AssetQuotes make_asset_quotes(Price reservation, Price half_spread, Price tick, Quantity size) {
    if (size <= 0) {
        throw std::invalid_argument("make_asset_quotes: size must be > 0");
    }
    const BidAskTicks ticks =
        quote_ticks(reservation - half_spread, reservation + half_spread, tick);

    AssetQuotes quotes;
    if (ticks.ask < 1) {
        return quotes;
    }
    quotes.ask = Quote{.price = price_at(ticks.ask, tick), .quantity = size};
    if (ticks.bid >= 1) {
        quotes.bid = Quote{.price = price_at(ticks.bid, tick), .quantity = size};
    }
    return quotes;
}

AvellanedaStoikovStrategy::AvellanedaStoikovStrategy(AvellanedaStoikovSettings settings)
    : settings_(std::move(settings)) {
    const Eigen::Index assets = settings_.params.k.size();
    if (assets == 0) {
        reject("at least one asset is required");
    }
    const auto count = static_cast<std::size_t>(assets);
    if (settings_.volatility.size() != count || settings_.quote_sizes.size() != count ||
        settings_.tick_sizes.size() != count || settings_.limits.max_abs_position.size() != count) {
        reject("volatility, quote sizes, tick sizes and limits must have one entry per asset");
    }
    if (settings_.correlation.rows() != assets || settings_.correlation.cols() != assets) {
        reject("the correlation matrix must be N x N");
    }
    if (settings_.min_samples < 0) {
        reject("min_samples must be >= 0");
    }

    liquidity_half_spread_ =
        math::avellaneda_stoikov::liquidity_half_spread(settings_.params.gamma, settings_.params.k);

    for (const Quantity size : settings_.quote_sizes) {
        if (size <= 0) {
            reject("every quote size must be > 0");
        }
    }
    for (const Price tick : settings_.tick_sizes) {
        detail::validate_tick(tick);
    }
    static_cast<void>(tau_at(settings_.horizon, 0.0));

    QuoteSet no_quotes(count);
    const math::Vector zeros = math::Vector::Zero(assets);
    apply_limits(no_quotes, settings_.limits, zeros, zeros);

    const std::vector<double> unit_vols(count, 1.0);
    settings_.correlation =
        math::covariance::CovarianceMatrix::from_correlation(unit_vols, settings_.correlation)
            .matrix();
}

math::Vector AvellanedaStoikovStrategy::volatilities() const {
    math::Vector sigma(static_cast<Eigen::Index>(asset_count()));
    for (std::size_t asset = 0; asset < asset_count(); ++asset) {
        sigma(static_cast<Eigen::Index>(asset)) = settings_.volatility[asset].sigma();
    }
    return sigma;
}

void AvellanedaStoikovStrategy::do_on_market_data(const math::Vector& mids, double dt) {
    if (!(mids.array() > 0.0).all()) {
        reject("mids must be > 0");
    }
    if (!previous_mids_.has_value()) {
        previous_mids_ = mids;
        return;
    }

    const double root_dt = std::sqrt(dt);
    for (Eigen::Index i = 0; i < mids.size(); ++i) {
        const double log_return = std::log(mids(i) / (*previous_mids_)(i));
        settings_.volatility[static_cast<std::size_t>(i)].update(log_return / root_dt);
    }
    previous_mids_ = mids;
}

QuoteSet AvellanedaStoikovStrategy::do_quote(const MarketView& view) const {
    const std::size_t count = asset_count();
    const double gamma = settings_.params.gamma;

    // A cold asset (no usable estimate yet) gets sigma 0: it is not quoted and
    // adds no risk to the others.
    math::Vector sigma(view.mids.size());
    for (std::size_t asset = 0; asset < count; ++asset) {
        const auto& estimator = settings_.volatility[asset];
        const double estimate = estimator.sigma();
        const bool warm = std::isfinite(estimate) && estimate > 0.0 &&
                          estimator.sample_count() >= settings_.min_samples;
        sigma(static_cast<Eigen::Index>(asset)) = warm ? estimate : 0.0;
    }

    const double tau = tau_at(settings_.horizon, view.time);
    const math::Vector sigma_q = sigma_times_q(sigma, settings_.correlation, view.inventory);
    const math::Vector reservation =
        view.mids - math::avellaneda_stoikov::risk_skew(gamma, tau, sigma_q);

    QuoteSet quotes(count);
    for (std::size_t asset = 0; asset < count; ++asset) {
        const auto index = static_cast<Eigen::Index>(asset);
        if (sigma(index) <= 0.0) {
            continue;
        }
        const double half_spread =
            liquidity_half_spread_(index) + (0.5 * gamma * tau * sigma(index) * sigma(index));
        quotes[asset] = make_asset_quotes(reservation(index),
                                          half_spread,
                                          settings_.tick_sizes[asset],
                                          settings_.quote_sizes[asset]);
    }

    apply_limits(quotes, settings_.limits, view.inventory, sigma_q);
    return quotes;
}

} // namespace mms::mm