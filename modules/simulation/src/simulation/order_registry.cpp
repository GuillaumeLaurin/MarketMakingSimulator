/// \file
/// Implementation of OrderRegistry (<simulation/order_registry.hpp>).

#include <stdexcept>

#include <simulation/order_registry.hpp>

namespace mms::simulation {

void OrderRegistry::add(mm::AssetId asset, mm::OrderId id, OrderOwner owner) {
    if (!owners_.emplace(std::pair{asset, id}, owner).second) {
        throw std::logic_error("OrderRegistry: this order is already recorded");
    }
}

std::optional<OrderOwner> OrderRegistry::find(mm::AssetId asset, mm::OrderId id) const {
    const auto found = owners_.find(std::pair{asset, id});
    if (found == owners_.end()) {
        return std::nullopt;
    }
    return found->second;
}

bool OrderRegistry::remove(mm::AssetId asset, mm::OrderId id) {
    return owners_.erase(std::pair{asset, id}) > 0;
}

} // namespace mms::simulation
