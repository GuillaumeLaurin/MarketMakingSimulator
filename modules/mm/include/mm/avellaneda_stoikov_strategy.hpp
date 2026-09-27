#ifndef MMS_MM_AVELLANEDA_STOIKOV_STRATEGY_HPP
#define MMS_MM_AVELLANEDA_STOIKOV_STRATEGY_HPP

/// \file
/// The Avellaneda-Stoikov quoting strategy, in its portfolio form. It composes
/// the formulas math provides: quotes are placed around the reservation price
/// r = mid - gamma * tau * (Sigma q), symmetrically by a half-spread of
/// (1/gamma) ln(1 + gamma/k) + gamma * tau * sigma^2 / 2, then rounded to the
/// tick, then filtered by the risk limits. Sigma is rebuilt each tick from the
/// configured correlations and the volatilities the strategy estimates itself.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <mm/horizon.hpp>
#include <mm/mm_export.hpp>
#include <mm/quote.hpp>
#include <mm/quoting_strategy.hpp>
#include <mm/risk.hpp>
#include <mm/types.hpp>

#include <math/avellaneda_stoikov.hpp>
#include <math/stats/volatility.hpp>
#include <math/types.hpp>

#include "math/stats/traits.hpp"

namespace mms::mm {

/// Sigma * q where Sigma = D R D, D = diag(sigma) and R the correlation
/// matrix: sigma_i * sum_j R_ij * sigma_j * q_j. Built directly, without ever
/// forming Sigma or its Cholesky factor.
[[nodiscard]] MMS_MM_EXPORT math::Vector
sigma_times_q(const math::Vector& sigma, const math::Matrix& correlation, const math::Vector& q);

/// The quotes on one asset for a given reservation price and half-spread:
/// bid rounded down and ask rounded up to the tick (never crossed, at least
/// one tick apart), each of `size` shares. A bid that would land below one
/// tick is dropped, and so are both sides when even the ask would.
[[nodiscard]] MMS_MM_EXPORT AssetQuotes make_asset_quotes(Price reservation,
                                                          Price half_spread,
                                                          Price tick,
                                                          Quantity size);

/// Everything the strategy is built from. One entry per asset in every vector,
/// in AssetId order (their common size is the number of assets).
struct AvellanedaStoikovSettings {
    /// gamma (> 0) and k (> 0, one per asset).
    math::avellaneda_stoikov::AvellanedaStoikovParams params;

    /// N x N correlation matrix: symmetric, positive definite.
    math::Matrix correlation;

    /// One ready volatility estimator per asset; the strategy feeds it.
    std::vector<math::stats::AnyVolatilityEstimator> volatility;

    HorizonPolicy horizon;
    std::vector<Quantity> quote_sizes;
    std::vector<Price> tick_sizes;
    RiskLimits limits;

    /// An asset is not quoted until its estimator has seen this many samples
    /// (an EWMA has no bias correction: its first samples understate sigma).
    std::int64_t min_samples = 0;
};

class MMS_MM_EXPORT AvellanedaStoikovStrategy final : public QuotingStrategy {
public:
    /// Validates every part of `settings` and throws std::invalid_argument on
    /// the first problem: sizes that disagree, an invalid gamma, k, tick, size
    /// or horizon, a correlation that is not a valid positive-definite matrix.
    explicit AvellanedaStoikovStrategy(AvellanedaStoikovSettings settings);

    [[nodiscard]] std::size_t asset_count() const noexcept override {
        return settings_.quote_sizes.size();
    }

    /// The current volatility estimate of every asset; NaN while an estimator
    /// has too few samples to say.
    [[nodiscard]] math::Vector volatilities() const;

protected:
    /// The first call only records the mids (call it once with the initial
    /// mids). Every later call feeds each estimator log(m_t / m_{t-1}) /
    /// sqrt(dt). Throws std::invalid_argument for a mid <= 0, changing nothing.
    void do_on_market_data(const math::Vector& mids, double dt) override;

    [[nodiscard]] QuoteSet do_quote(const MarketView& view) const override;

private:
    AvellanedaStoikovSettings settings_;
    math::Vector liquidity_half_spread_; ///< (1/gamma) ln(1 + gamma/k), precomputed
    std::optional<math::Vector> previous_mids_;
};

} // namespace mms::mm

#endif // MMS_MM_AVELLANEDA_STOIKOV_STRATEGY_HPP