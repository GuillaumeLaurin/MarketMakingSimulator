#ifndef MMS_MM_RISK_HPP
#define MMS_MM_RISK_HPP

/// \file
/// Risk limits, applied to the quotes a strategy wants to post. A breach
/// never throws: being long or short is the normal state of a market maker,
/// so the response is only to stop quoting the side that would make things
/// worse. Reducing exposure is always allowed, even when already past a limit.

#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

#include <mm/quote.hpp>
#include <mm/types.hpp>

#include <math/avellaneda_stoikov.hpp>
#include <math/types.hpp>

namespace mms::mm {

struct RiskLimits {
    /// Largest |position| allowed on each asset, in AssetId order. Every
    /// entry must be > 0
    std::vector<Quantity> max_abs_position;

    /// Cap on the portfolio risk q^T Sigma q. Disabled when empty. Must be
    /// finite and > 0 when set.
    std::optional<double> max_portfolio_risk;
};

/// May a quote of `quantity` on `side` be posted, given the current
/// `position`? Yes if a full fill keeps |position| within `max_abs`, or if it
/// at least brings |position| down (so an asset already past the limit can
/// still quote the side that reduces it).
[[nodiscard]] inline bool
position_limit_allows(double position, Side side, Quantity quantity, Quantity max_abs) noexcept {
    const double filled = to_double(quantity);
    const double after = (side == Side::Buy) ? position + filled : position - filled;
    return std::fabs(after) <= to_double(max_abs) || std::fabs(after) < std::fabs(position);
}

/// While the portfolio is over its risk cap: may `side` be quoted on an
/// asset whose entry in Sigma * q is `sigma_q_i`? The gradient of q^T Sigma q
/// with respect to q_i is 2 * (Sigma q)_i, so to first order a buy raises the
/// risk when that entry is positive and a sell raises it when it is negative.
[[nodiscard]] inline bool portfolio_cap_allows(Side side, double sigma_q_i) noexcept {
    return (side == Side::Buy) ? sigma_q_i <= 0.0 : sigma_q_i >= 0.0;
}

namespace detail {

inline void validate_risk_inputs(const QuoteSet& quotes,
                                 const RiskLimits& limits,
                                 const math::Vector& q,
                                 const math::Vector& sigma_q) {
    const std::size_t assets = quotes.size();

    if (limits.max_abs_position.size() != assets || static_cast<std::size_t>(q.size()) != assets ||
        static_cast<std::size_t>(sigma_q.size()) != assets) {
        throw std::invalid_argument(
            "risk: quotes, limits, q and sigma_q must have one entry per asset");
    }

    for (const Quantity max_abs : limits.max_abs_position) {
        if (max_abs <= 0) {
            throw std::invalid_argument("risk: every max position must be > 0");
        }
    }

    if (limits.max_portfolio_risk.has_value() &&
        (!std::isfinite(*limits.max_portfolio_risk) || *limits.max_portfolio_risk <= 0.0)) {
        throw std::invalid_argument("risk: the portfolio risk cap must be finite and > 0");
    }
    if (!q.allFinite()) {
        throw std::invalid_argument("risk: inventory must be finite");
    }
    if (limits.max_portfolio_risk.has_value() && !sigma_q.allFinite()) {
        throw std::invalid_argument("risk: sigma * q must be finite when the risk cap is enabled");
    }
}

} // namespace detail

/// Removes, in place, every quote the limits do not allow: a side whose full
/// fill would break the asset's position limit, and, while q^T Sigma q is
/// above the portfolio cap, a side that raises the portfolio risk. `q` is the
/// signed inventory per asset and `sigma_q` is Sigma * q. Throws
/// std::invalid_argument on inconsistent sizes or invalid limits, never for a
/// breach.
inline void apply_limits(QuoteSet& quotes,
                         const RiskLimits& limits,
                         const math::Vector& q,
                         const math::Vector& sigma_q) {
    detail::validate_risk_inputs(quotes, limits, q, sigma_q);

    const bool over_cap =
        limits.max_portfolio_risk.has_value() &&
        math::avellaneda_stoikov::inventory_risk(q, sigma_q) > *limits.max_portfolio_risk;

    for (AssetId asset = 0; asset < quotes.size(); ++asset) {
        const auto index = static_cast<Eigen::Index>(asset);
        for (const Side side : {Side::Buy, Side::Sell}) {
            std::optional<Quote>& quote = quotes[asset].on(side);
            if (!quote.has_value()) {
                continue;
            }
            const bool position_ok = position_limit_allows(
                q(index), side, quote->quantity, limits.max_abs_position[asset]);
            const bool cap_ok = !over_cap || portfolio_cap_allows(side, sigma_q(index));
            if (!position_ok || !cap_ok) {
                quote.reset();
            }
        }
    }
}

} // namespace mms::mm

#endif // MMS_MM_RISK_HPP