#ifndef MMS_SIMULATION_AGENT_ORDERS_HPP
#define MMS_SIMULATION_AGENT_ORDERS_HPP

/// \file
/// AgentOrders: the orders of one agent that have not finished, and the state each is in.
/// This is what the agent is shown as AgentView::open_orders, so the list is kept in
/// exactly that form and a span over it is handed out as it is. It is driven by the loop:
///
///   on_accepted   the gateway accepted an order: it is on its way (Submitting)
///   on_arrived    it reached the exchange and rests there (Resting)
///   on_filled     part of it traded; at zero it is gone
///   request_cancel  a cancel is on its way (Cancelling)
///   on_done       it is over without resting, or its cancel went through
///
/// An order is named by its Ticket until it has an id, and by its asset and id afterwards.
/// The list keeps the order the orders were accepted in. It tracks the view of the orders;
/// what they hold back is the Account's business, which uses the same tickets.

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <agents/agent_view.hpp>
#include <mm/types.hpp>

#include <simulation/account.hpp>
#include <simulation/simulation_export.hpp>

namespace mms::simulation {

class MMS_SIMULATION_EXPORT AgentOrders {
public:
    /// A new order on its way, with no id yet. Throws std::invalid_argument for a
    /// quantity <= 0 and std::logic_error for a ticket already in the list.
    void on_accepted(
        Ticket ticket, mm::AssetId asset, mm::Side side, mm::Price price, mm::Quantity quantity);

    /// The order reached the exchange and rests there under `id`. Throws
    /// std::out_of_range for an unknown ticket and std::logic_error if the order is not
    /// on its way.
    void on_arrived(Ticket ticket, mm::OrderId id);

    /// `quantity` of the order traded; the order leaves the list when nothing is left of
    /// it. Valid in any state: an order whose cancel is on its way can still trade. Throws
    /// std::out_of_range for an unknown ticket and std::invalid_argument for a quantity
    /// that is <= 0 or more than what is left.
    void on_filled(Ticket ticket, mm::Quantity quantity);

    /// A cancel is on its way for the resting order `id` of `asset`. Returns false, and
    /// changes nothing, if no such order is resting: it is unknown, still on its way, or
    /// already being cancelled.
    bool request_cancel(mm::AssetId asset, mm::OrderId id);

    /// The order is over: an immediate order that did not rest, or a cancel that went
    /// through. Removes it. Returns false for a ticket that is not in the list, which is
    /// not an error.
    bool on_done(Ticket ticket);

    /// The ticket of the order that has reached the exchange under `id` for `asset`, if
    /// it is in the list. An order still on its way has no id, so it is never found.
    [[nodiscard]] std::optional<Ticket> ticket_of(mm::AssetId asset, mm::OrderId id) const;

    /// The orders, oldest first.
    [[nodiscard]] std::span<const agents::OpenOrder> orders() const noexcept { return orders_; }

    [[nodiscard]] std::size_t size() const noexcept { return orders_.size(); }

private:
    [[nodiscard]] std::size_t index_of(Ticket ticket) const; // std::out_of_range if absent

    std::vector<Ticket> tickets_;           ///< same index as orders_
    std::vector<agents::OpenOrder> orders_; ///< what the agent sees
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_AGENT_ORDERS_HPP
