#ifndef MMS_AGENTS_BOOK_HPP
#define MMS_AGENTS_BOOK_HPP

/// \file
/// Reading a price off what an agent is shown of the market.

#include <optional>

#include <agents/agent_view.hpp>
#include <mm/types.hpp>

namespace mms::agents {

/// The price an agent that does not know the true one goes by: the middle of the best bid
/// and the best ask. When one of the two sides is empty, the price of the last trade on the
/// tape; when there is none either, nothing. An agent that gets nothing does not act on
/// that asset.
[[nodiscard]] inline std::optional<mm::Price> book_mid(const AssetView& asset) noexcept {
    if (!asset.bids.empty() && !asset.asks.empty()) {
        return (asset.bids.front().price + asset.asks.front().price) / 2.0;
    }
    if (!asset.recent_trades.empty()) {
        return asset.recent_trades.back().price;
    }
    return std::nullopt;
}

} // namespace mms::agents

#endif // MMS_AGENTS_BOOK_HPP
