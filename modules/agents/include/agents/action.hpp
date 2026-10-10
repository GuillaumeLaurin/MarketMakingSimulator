#ifndef MMS_AGENTS_ACTION_HPP
#define MMS_AGENTS_ACTION_HPP

/// \file
/// What an agent can ask for. Plain data, nothing that points back into the
/// engine, so an action can be copied, logged, journalled and one day passed
/// across a plugin boundary.

#include <variant>

#include <mm/types.hpp>

namespace mms::agents {

/// A limit order. quantity > 0 and price > 0, checked by the gateway.
struct SubmitLimit {
    mm::AssetId asset;
    mm::Side side;
    mm::Price price;
    mm::Quantity quantity;
    mm::TimeInForce time_in_force = mm::TimeInForce::GTC;
};

/// A market order: it takes whatever the book offers and never rests.
struct SubmitMarket {
    mm::AssetId asset;
    mm::Side side;
    mm::Quantity quantity;
    mm::MarketTimeInForce time_in_force = mm::MarketTimeInForce::IOC;
};

/// Cancel one of the agent's own resting orders, named by the id found in
/// AgentView::open_orders. Cancelling an order that has already gone is not
/// an error.
struct Cancel {
    mm::AssetId asset;
    mm::OrderId id;
};

using Action = std::variant<SubmitLimit, SubmitMarket, Cancel>;

} // namespace mms::agents

#endif // MMS_AGENTS_ACTION_HPP