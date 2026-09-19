#ifndef MMS_MM_QUOTE_HPP
#define MMS_MM_QUOTE_HPP

/// \file
/// What a quoting strategy hands back: for each asset, at most one bid and
/// at most one ask it wants to have resting. An empty side means "do not
/// quote there" (position limit reached, portfolio cap, ...). That is not
/// the same thing as a quote of zero size, and it cannot be expressed as one.

#include <optional>
#include <vector>

#include <mm/types.hpp>

namespace mms::mm {

/// A price and a size. A plain aggregate with no validation: the strategy is
/// responsible for never producing a non-positive price or quantity, and
/// "no quote" is an empty optional in AssetQuotes, never a zero quantity.
struct Quote {
    Price price;
    Quantity quantity;

    [[nodiscard]] bool operator==(const Quote&) const = default;
};

/// The desired quotes on one asset. Default-constructed means "quote
/// nothing on either side".
struct AssetQuotes {
    std::optional<Quote> bid;
    std::optional<Quote> ask;

    /// The bid for Side::Buy, the ask for Side::Sell, so callers that treat
    /// both sides alike (risk suppression, requote planning) need no branch.
    [[nodiscard]] const std::optional<Quote>& on(Side side) const noexcept {
        return side == Side::Buy ? bid : ask;
    }
    
    [[nodiscard]] std::optional<Quote>& on(Side side) noexcept {
        return side == Side::Buy ? bid : ask;
    }

    [[nodiscard]] bool operator==(const AssetQuotes&) const = default;
};

/// One AssetQuotes per asset, indexed by AssetId. Its size is the number of
/// assets in the simulation.
using QuoteSet = std::vector<AssetQuotes>;

/// A quote is a passive resting order, so it always goes out Good-Till-Cancel.
/// This is the one place that knowledge lives; simulation does not repeat it.
[[nodiscard]] inline LimitOrder to_limit_order(Side side, const Quote& quote) noexcept {
    return LimitOrder{
        .side = side, 
        .price = quote.price, 
        .quantity = quote.quantity, 
        .time_in_force = TimeInForce::GTC
    };
}

} // namespace mms::mm

#endif // MMS_MM_QUOTE_HPP