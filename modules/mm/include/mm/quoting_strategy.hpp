#ifndef MMS_MM_QUOTING_STRATEGY_HPP
#define MMS_MM_QUOTING_STRATEGY_HPP

/// \file
/// QuotingStrategy: the interface every quoting policy implements, and the
/// MarketView it is given. Like PriceProcess it is a pure virtual base picked
/// from config at construction and called once per tick, where a virtual call
/// is negligible. The two public entry points are non-virtual and enforce the
/// contract identically for every strategy (Liskov: no subclass can loosen it);
/// the protected virtuals do the work.

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <mm/quote.hpp>
#include <mm/types.hpp>

#include <math/types.hpp>

namespace mms::mm {

/// What a strategy sees when it is asked to quote: the current mid of every
/// asset, the signed inventory (as doubles, converted once here so no
/// strategy ever converts a Quantity itself) and the simulation time. It owns
/// its vectors, so it can never dangle.
struct MarketView {
    math::Vector mids;
    math::Vector inventory;
    double time;
};

/// The one place a vector of Quantity becomes a vector of doubles.
[[nodiscard]] inline math::Vector inventory_vector(const std::vector<Quantity>& quantities) {
    math::Vector inventory(static_cast<Eigen::Index>(quantities.size()));
    for (std::size_t asset = 0; asset < quantities.size(); ++asset) {
        inventory(static_cast<Eigen::Index>(asset)) = to_double(quantities[asset]);
    }
    return inventory;
}

[[nodiscard]] inline MarketView
make_market_view(math::Vector mids, const std::vector<Quantity>& quantities, double time) {
    return MarketView{
        .mids = std::move(mids), .inventory = inventory_vector(quantities), .time = time};
}

class QuotingStrategy {
public:
    virtual ~QuotingStrategy() = default;

    /// Number of assets this strategy quotes.
    [[nodiscard]] virtual std::size_t asset_count() const noexcept = 0;

    /// Feeds the strategy the new mids, `dt` after the previous call, so it
    /// can update whatever it estimates from them. Throws
    /// std::invalid_argument unless dt is finite and STRICTLY positive (the
    /// strategies divide returns by sqrt(dt), so 0 is not allowed here, unlike
    /// PriceProcess::advance), the mids have one entry per asset and every mid
    /// is finite.
    void on_market_data(const math::Vector& mids, double dt) {
        if (!std::isfinite(dt) || dt <= 0.0) {
            throw std::invalid_argument(
                "QuotingStrategy::on_market_data: dt must be finite and > 0");
        }
        require_one_per_asset(mids.size(), "mids");
        if (!mids.allFinite()) {
            throw std::invalid_argument("QuotingStrategy::on_market_data: mids must be finite");
        }
        do_on_market_data(mids, dt);
    }

    /// The quotes the strategy wants live right now: one AssetQuotes per
    /// asset, an empty side meaning "do not quote there". Throws
    /// std::invalid_argument for a view that does not have one entry per asset
    /// or holds a non-finite value, and std::logic_error if the concrete
    /// strategy returns a QuoteSet of the wrong size.
    [[nodiscard]] QuoteSet quote(const MarketView& view) const {
        require_one_per_asset(view.mids.size(), "view mids");
        require_one_per_asset(view.inventory.size(), "view inventory");
        if (!view.mids.allFinite() || !view.inventory.allFinite() || !std::isfinite(view.time)) {
            throw std::invalid_argument("QuotingStrategy::quote: the view must be finite");
        }
        QuoteSet quotes = do_quote(view);
        if (quotes.size() != asset_count()) {
            throw std::logic_error(
                "QuotingStrategy::quote: the strategy returned the wrong number of assets");
        }
        return quotes;
    }

protected:
    QuotingStrategy() = default;
    QuotingStrategy(const QuotingStrategy&) = default;
    QuotingStrategy& operator=(const QuotingStrategy&) = default;
    QuotingStrategy(QuotingStrategy&&) noexcept = default;
    QuotingStrategy& operator=(QuotingStrategy&&) noexcept = default;

    virtual void do_on_market_data(const math::Vector& mids, double dt) = 0;

    [[nodiscard]] virtual QuoteSet do_quote(const MarketView& view) const = 0;

private:
    void require_one_per_asset(Eigen::Index size, const char* what) const {
        if (static_cast<std::size_t>(size) != asset_count()) {
            throw std::invalid_argument(std::string("QuotingStrategy: ") + what +
                                        " must have one entry per asset");
        }
    }
};

} // namespace mms::mm

#endif // MMS_MM_QUOTING_STRATEGY_HPP