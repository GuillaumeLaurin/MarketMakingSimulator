#ifndef MMS_SIMULATION_ORDER_REGISTRY_HPP
#define MMS_SIMULATION_ORDER_REGISTRY_HPP

/// \file
/// OrderRegistry: who an order that rests at an exchange belongs to. The matching engines
/// know nothing of agents, so when a trade names an order id, the loop asks here whose it
/// is and which of that agent's tickets it carries.
///
/// An order id is only unique within one asset's engine (every engine numbers its orders
/// from 1), so the key is the asset and the id together.

#include <cstddef>
#include <map>
#include <optional>
#include <utility>

#include <mm/types.hpp>

#include <simulation/account.hpp>
#include <simulation/agent_id.hpp>
#include <simulation/simulation_export.hpp>

namespace mms::simulation {

struct OrderOwner {
    AgentId agent;
    Ticket ticket;

    friend bool operator==(const OrderOwner&, const OrderOwner&) = default;
};

class MMS_SIMULATION_EXPORT OrderRegistry {
public:
    /// Records that order `id` of `asset` belongs to `owner`. Throws std::logic_error if
    /// that order is already recorded: an engine never hands out an id twice, so it would
    /// mean the registry is out of step with the books.
    void add(mm::AssetId asset, mm::OrderId id, OrderOwner owner);

    /// The owner of order `id` of `asset`, or nothing if it is not recorded.
    [[nodiscard]] std::optional<OrderOwner> find(mm::AssetId asset, mm::OrderId id) const;

    /// Forgets the order. Returns false if it was not recorded, which is not an error: an
    /// order that was filled and one that was cancelled both end here, in either order.
    bool remove(mm::AssetId asset, mm::OrderId id);

    /// How many orders are recorded.
    [[nodiscard]] std::size_t size() const noexcept { return owners_.size(); }

private:
    std::map<std::pair<mm::AssetId, mm::OrderId>, OrderOwner> owners_;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_ORDER_REGISTRY_HPP
