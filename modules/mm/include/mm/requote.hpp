#ifndef MMS_MM_REQUOTE_HPP
#define MMS_MM_REQUOTE_HPP

/// \file
/// The requote plan: given the quotes a strategy wants and the quotes that are
/// resting now, decide what to cancel and what to submit. Pure, and blind to
/// engines and clocks: simulation keeps the list of live quotes and executes
/// the plan. A quote is only replaced when its price moved by at least a
/// threshold of ticks (or its size changed), because cancelling a quote that
/// is still good throws away its place in the queue.

#include <cstddef>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <vector>

#include <mm/quote.hpp>
#include <mm/tick.hpp>
#include <mm/types.hpp>

namespace mms::mm {

/// A quote resting in the book: its id and its price and REMAINING quantity.
/// After a partial fill the quantity is smaller than what was submitted;
/// simulation tracks it from the fills.
struct LiveQuote {
    OrderId id;
    Quote quote;
};

struct AssetLiveQuotes {
    std::optional<LiveQuote> bid;
    std::optional<LiveQuote> ask;

    [[nodiscard]] const std::optional<LiveQuote>& on(Side side) const noexcept {
        return side == Side::Buy ? bid : ask;
    }
};

/// One AssetLiveQuotes per asset, indexed by AssetId.
using LiveQuoteSet = std::vector<AssetLiveQuotes>;

struct CancelRequest {
    AssetId asset;
    OrderId id;
};

struct SubmitRequest {
    AssetId asset;
    Side side;
    Quote quote;
};

/// What to send. Assets in ascending order, Buy before Sell. A replaced quote
/// shows up twice, as a cancel and as a submit: execute the cancel first.
struct RequotePlan {
    std::vector<CancelRequest> cancels;
    std::vector<SubmitRequest> submits;
};

/// Should the resting quote be replaced by the wanted one? Yes if the size
/// differs (a partial fill left it smaller, so it is restocked) or if the price
/// moved by at least `threshold` ticks. A threshold of 0 therefore replaces
/// every quote at every call, even an identical one.
[[nodiscard]] inline bool
needs_replacing(const Quote& resting, const Quote& wanted, Price tick, Ticks threshold) {
    if (resting.quantity != wanted.quantity) {
        return true;
    }
    const Ticks moved =
        std::abs(nearest_ticks(wanted.price, tick) - nearest_ticks(resting.price, tick));
    return moved >= threshold;
}

/// Compares what is wanted with what is live, side by side and asset by asset:
///   live, not wanted        -> cancel (this is how a side the risk limits
///                              suppressed gets its resting order pulled);
///   wanted, not live        -> submit;
///   both, needs_replacing   -> cancel and submit;
///   both, otherwise         -> nothing.
/// `tick_sizes` holds one tick per asset. Throws std::invalid_argument for a
/// negative threshold, sizes that do not match, or an invalid tick size, and
/// changes nothing when it does.
[[nodiscard]] inline RequotePlan plan_requote(const QuoteSet& desired,
                                              const LiveQuoteSet& live,
                                              const std::vector<Price>& tick_sizes,
                                              Ticks threshold) {
    if (threshold < 0) {
        throw std::invalid_argument("requote: the threshold must be >= 0");
    }
    if (live.size() != desired.size() || tick_sizes.size() != desired.size()) {
        throw std::invalid_argument(
            "requote: desired, live and tick_sizes must have one entry per asset");
    }

    for (const Price tick : tick_sizes) {
        detail::validate_tick(tick);
    }

    RequotePlan plan;

    for (AssetId asset = 0; asset < desired.size(); ++asset) {
        for (const Side side : {Side::Buy, Side::Sell}) {
            const std::optional<Quote>& wanted = desired[asset].on(side);
            const std::optional<LiveQuote>& resting = live[asset].on(side);

            const bool replace =
                wanted.has_value() && resting.has_value() &&
                needs_replacing(resting->quote, *wanted, tick_sizes[asset], threshold);

            if (resting.has_value() && (!wanted.has_value() || replace)) {
                plan.cancels.push_back(CancelRequest{.asset = asset, .id = resting->id});
            }
            if (wanted.has_value() && (!resting.has_value() || replace)) {
                plan.submits.push_back(
                    SubmitRequest{.asset = asset, .side = side, .quote = *wanted});
            }
        }
    }

    return plan;
}

} // namespace mms::mm

#endif // MMS_MM_REQUOTE_HPP