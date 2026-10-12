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

/// Where an order of this agent stands. An order is on its way to the exchange for as
/// long as the latency of its message lasts, so an agent that acts every tick would
/// otherwise send the same quote again and again.
enum class OrderState : std::uint8_t {
    /// Accepted and sent, not at the exchange yet. It has no order id yet.
    Submitting,
    /// At the exchange, resting in the book.
    Resting,
    /// At the exchange, with a cancel on its way. It may still trade before the cancel
    /// arrives.
    Cancelling,
};

/// An order of this agent that has not finished: on its way, resting, or being cancelled.
struct OpenOrder {
    mm::AssetId asset;
    /// What a Cancel names. Only meaningful once the order has reached the exchange,
    /// that is when `state` is not Submitting; it is zero before.
    mm::OrderId id;
    mm::Side side;
    mm::Price price;
    /// What is left of it to trade.
    mm::Quantity remaining;
    OrderState state = OrderState::Resting;
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
    /// Time since this agent's previous act(), in the simulation's time unit, or since
    /// the start for the first call. A strategy that feeds an estimator with
    /// returns per unit of time needs it when the agent does not act every tick.
    double elapsed = 0.0;
    AccountView account;
    std::span<const OpenOrder> open_orders;
    /// One per asset, in AssetId order.
    std::span<const AssetView> assets;
};

} // namespace mms::agents

#endif // MMS_AGENTS_AGENT_VIEW_HPP