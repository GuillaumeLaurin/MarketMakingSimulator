#ifndef MMS_AGENTS_AGENT_VIEW_HPP
#define MMS_AGENTS_AGENT_VIEW_HPP

/// \file
/// What an agent is shown when it is asked to act. Spans point into buffers
/// owned by the loop and are valid only for the duration of the call; the
/// agent copies what it wants to keep. The true price of the price process is
/// not here: an agent that should know it gets AssetView::signal.

#include <cstdint>
#include <optional>
#include <span>

#include <mm/types.hpp>

namespace mms::agents {

/// Total resting quantity at one price.
struct BookLevel {
    mm::Price price;
    mm::Quantity quantity;
};

/// One executed trade, as seen on the tape.
struct TradePrint {
    mm::Price price;
    mm::Quantity quantity;
    /// Side of the aggressor: Buy means the trade was started by a buyer.
    mm::Side aggressor;
};

/// An order of this agent that is resting in a book.
struct OpenOrder {
    mm::AssetId asset;
    mm::OrderId id;
    mm::Side side;
    mm::Price price;
    mm::Quantity remaining;
};

/// The market in one asset, as this agent may see it.
struct AssetView {
    /// Best first: bids from the highest price down, asks from the lowest up.
    /// At most the K best levels of each side; K is a setting of the run.
    std::span<const BookLevel> bids;
    std::span<const BookLevel> asks;
    /// Most recent last. Only the trades since the agent last looked, up to a limit.
    std::span<const TradePrint> recent_trades;
    /// The true price, possibly with noise added, for agents configured as
    /// informed. Empty for everyone else.
    std::optional<mm::Price> signal;
};

/// The agent's own account.
struct AccountView {
    double cash;
    /// What it can still spend: cash minus what resting buys reserve and
    /// minus the collateral held for short positions.
    double buying_power;
    /// Signed quantity held, one per asset, in AssetId order.
    std::span<const mm::Quantity> positions;
};

struct AgentView {
    std::int64_t tick;
    double time;
    AccountView account;
    std::span<const OpenOrder> open_orders;
    /// One per asset, in AssetId order.
    std::span<const AssetView> assets;
};

} // namespace mms::agents

#endif // MMS_AGENTS_AGENT_VIEW_HPP