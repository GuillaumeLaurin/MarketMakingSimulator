/// \file
/// Implementation of AgentOrders (<simulation/agent_orders.hpp>).

#include <algorithm>
#include <stdexcept>

#include <simulation/agent_orders.hpp>

namespace mms::simulation {

std::size_t AgentOrders::index_of(Ticket ticket) const {
    const auto found = std::ranges::find(tickets_, ticket);
    if (found == tickets_.end()) {
        throw std::out_of_range("AgentOrders: no such ticket");
    }
    return static_cast<std::size_t>(found - tickets_.begin());
}

void AgentOrders::on_accepted(
    Ticket ticket, mm::AssetId asset, mm::Side side, mm::Price price, mm::Quantity quantity) {
    if (quantity <= 0) {
        throw std::invalid_argument("AgentOrders: an order needs a quantity > 0");
    }
    if (std::ranges::find(tickets_, ticket) != tickets_.end()) {
        throw std::logic_error("AgentOrders: this ticket is already in the list");
    }
    tickets_.push_back(ticket);
    orders_.push_back(agents::OpenOrder{.asset = asset,
                                        .id = mm::OrderId{0},
                                        .side = side,
                                        .price = price,
                                        .remaining = quantity,
                                        .state = agents::OrderState::Submitting});
}

void AgentOrders::on_arrived(Ticket ticket, mm::OrderId id) {
    agents::OpenOrder& order = orders_[index_of(ticket)];
    if (order.state != agents::OrderState::Submitting) {
        throw std::logic_error("AgentOrders: this order is not on its way");
    }
    order.id = id;
    order.state = agents::OrderState::Resting;
}

void AgentOrders::on_filled(Ticket ticket, mm::Quantity quantity) {
    const std::size_t index = index_of(ticket);
    agents::OpenOrder& order = orders_[index];
    if (quantity <= 0 || quantity > order.remaining) {
        throw std::invalid_argument(
            "AgentOrders: a fill must be > 0 and no more than what is left");
    }
    order.remaining -= quantity;
    if (order.remaining == 0) {
        tickets_.erase(tickets_.begin() + static_cast<std::ptrdiff_t>(index));
        orders_.erase(orders_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

bool AgentOrders::request_cancel(mm::AssetId asset, mm::OrderId id) {
    for (agents::OpenOrder& order : orders_) {
        if (order.asset == asset && order.id == id && order.state == agents::OrderState::Resting) {
            order.state = agents::OrderState::Cancelling;
            return true;
        }
    }
    return false;
}

bool AgentOrders::on_done(Ticket ticket) {
    const auto found = std::ranges::find(tickets_, ticket);
    if (found == tickets_.end()) {
        return false;
    }
    const auto offset = found - tickets_.begin();
    tickets_.erase(found);
    orders_.erase(orders_.begin() + offset);
    return true;
}

std::optional<Ticket> AgentOrders::ticket_of(mm::AssetId asset, mm::OrderId id) const {
    for (std::size_t index = 0; index < orders_.size(); ++index) {
        const agents::OpenOrder& order = orders_[index];
        if (order.asset == asset && order.id == id &&
            order.state != agents::OrderState::Submitting) {
            return tickets_[index];
        }
    }
    return std::nullopt;
}

} // namespace mms::simulation
