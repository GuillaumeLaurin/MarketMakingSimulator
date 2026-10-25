/// \file
/// Tests for simulation/agent_orders.hpp. The list is driven the way the loop will drive it:
/// an order is accepted (on its way), arrives (rests), trades, is cancelled or ends. What is
/// checked is the list an agent would be shown after each step: its content, its order, the
/// state of each order and what each call refuses.

#include <cstddef>
#include <optional>
#include <stdexcept>

#include <agents/agent_view.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/types.hpp>

#include <simulation/account.hpp>
#include <simulation/agent_orders.hpp>

namespace {

namespace sim = mms::simulation;
namespace agents = mms::agents;
namespace mm = mms::mm;

using agents::OrderState;
using mm::Side;

constexpr sim::Ticket t1{1};
constexpr sim::Ticket t2{2};
constexpr sim::Ticket t3{3};

} // namespace

TEST_CASE("A new list is empty", "[simulation][agent_orders]") {
    const sim::AgentOrders orders;

    CHECK(orders.size() == 0);
    CHECK(orders.orders().empty());
}

TEST_CASE("An accepted order is on its way, with no id", "[simulation][agent_orders]") {
    sim::AgentOrders orders;

    orders.on_accepted(t1, 2, Side::Sell, 101.5, 40);

    REQUIRE(orders.size() == 1);
    const agents::OpenOrder& order = orders.orders()[0];
    CHECK(order.asset == 2);
    CHECK(order.side == Side::Sell);
    CHECK(order.price == 101.5);
    CHECK(order.remaining == 40);
    CHECK(order.state == OrderState::Submitting);
    CHECK(order.id == mm::OrderId{0});
}

TEST_CASE("An order that arrives rests under its id", "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 99.0, 10);

    orders.on_arrived(t1, mm::OrderId{17});

    const agents::OpenOrder& order = orders.orders()[0];
    CHECK(order.state == OrderState::Resting);
    CHECK(order.id == mm::OrderId{17});
    CHECK(order.remaining == 10);
}

TEST_CASE("The orders are listed oldest first, whatever happens to the others",
          "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 1.0, 1);
    orders.on_accepted(t2, 1, Side::Sell, 2.0, 2);
    orders.on_accepted(t3, 2, Side::Buy, 3.0, 3);

    REQUIRE(orders.size() == 3);
    CHECK(orders.orders()[0].asset == 0);
    CHECK(orders.orders()[1].asset == 1);
    CHECK(orders.orders()[2].asset == 2);

    CHECK(orders.on_done(t2));

    REQUIRE(orders.size() == 2);
    CHECK(orders.orders()[0].asset == 0);
    CHECK(orders.orders()[1].asset == 2);
}

TEST_CASE("A partial fill leaves the order with what is left, in any state",
          "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 10);

    orders.on_filled(t1, 3); // it can trade while still on its way: the fill comes with the submit
    CHECK(orders.orders()[0].remaining == 7);
    CHECK(orders.orders()[0].state == OrderState::Submitting);

    orders.on_arrived(t1, mm::OrderId{1});
    orders.on_filled(t1, 2);
    CHECK(orders.orders()[0].remaining == 5);
    CHECK(orders.orders()[0].state == OrderState::Resting);

    REQUIRE(orders.request_cancel(0, mm::OrderId{1}));
    orders.on_filled(t1, 1); // the cancel is on its way and the order still trades
    CHECK(orders.orders()[0].remaining == 4);
    CHECK(orders.orders()[0].state == OrderState::Cancelling);
}

TEST_CASE("An order with nothing left leaves the list", "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 10);
    orders.on_accepted(t2, 0, Side::Sell, 101.0, 5);
    orders.on_arrived(t1, mm::OrderId{1});

    orders.on_filled(t1, 10);

    REQUIRE(orders.size() == 1);
    CHECK(orders.orders()[0].side == Side::Sell);
    CHECK_FALSE(orders.ticket_of(0, mm::OrderId{1}).has_value());
}

TEST_CASE("An order that fills completely before it arrives never appears again",
          "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 4);

    orders.on_filled(t1, 4);

    CHECK(orders.size() == 0);
    CHECK_FALSE(orders.on_done(t1)); // the loop may still call it, and that is fine
}

TEST_CASE("Only a resting order can be asked to cancel, and only once",
          "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 10);
    orders.on_accepted(t2, 0, Side::Sell, 101.0, 10);
    orders.on_arrived(t2, mm::OrderId{4});

    CHECK_FALSE(orders.request_cancel(0, mm::OrderId{0})); // t1 has no id yet
    CHECK(orders.request_cancel(0, mm::OrderId{4}));
    CHECK(orders.orders()[1].state == OrderState::Cancelling);
    CHECK_FALSE(orders.request_cancel(0, mm::OrderId{4})); // already on its way
    CHECK_FALSE(orders.request_cancel(0, mm::OrderId{99}));
    CHECK_FALSE(orders.request_cancel(1, mm::OrderId{4})); // another asset's order 4
    CHECK(orders.orders()[0].state == OrderState::Submitting);
}

TEST_CASE("A cancel names an asset and an id together", "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 10);
    orders.on_accepted(t2, 1, Side::Buy, 100.0, 10);
    orders.on_arrived(t1, mm::OrderId{1});
    orders.on_arrived(t2, mm::OrderId{1}); // the same id, on the other asset

    CHECK(orders.request_cancel(1, mm::OrderId{1}));

    CHECK(orders.orders()[0].state == OrderState::Resting); // asset 0's order is not touched
    CHECK(orders.orders()[1].state == OrderState::Cancelling);
}

TEST_CASE("A cancelled order leaves the list", "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 10);
    orders.on_arrived(t1, mm::OrderId{1});
    REQUIRE(orders.request_cancel(0, mm::OrderId{1}));

    CHECK(orders.on_done(t1));

    CHECK(orders.size() == 0);
    CHECK_FALSE(orders.ticket_of(0, mm::OrderId{1}).has_value());
}

TEST_CASE("The ticket of an order is found by asset and id once it has reached the exchange",
          "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 10);
    orders.on_accepted(t2, 1, Side::Buy, 100.0, 10);
    orders.on_accepted(t3, 1, Side::Sell, 101.0, 10);
    orders.on_arrived(t1, mm::OrderId{1});
    orders.on_arrived(t2, mm::OrderId{1}); // the same id on another asset
    // t3 is still on its way

    CHECK(orders.ticket_of(0, mm::OrderId{1}) == t1);
    CHECK(orders.ticket_of(1, mm::OrderId{1}) == t2);
    CHECK_FALSE(orders.ticket_of(2, mm::OrderId{1}).has_value());
    CHECK_FALSE(orders.ticket_of(1, mm::OrderId{0}).has_value()); // an order on its way has no id
}

TEST_CASE("An order being cancelled is still found by its id", "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 10);
    orders.on_arrived(t1, mm::OrderId{6});
    REQUIRE(orders.request_cancel(0, mm::OrderId{6}));

    CHECK(orders.ticket_of(0, mm::OrderId{6}) == t1);
}

TEST_CASE("An order that does not rest is dropped when it is done", "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 10); // an immediate order, partly filled
    orders.on_filled(t1, 6);

    CHECK(orders.on_done(t1));

    CHECK(orders.size() == 0);
}

TEST_CASE("on_done reports false for a ticket that is not there", "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 10);

    CHECK_FALSE(orders.on_done(t2));
    CHECK(orders.size() == 1);
}

TEST_CASE("Calls the loop should never make are refused", "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 10);

    CHECK_THROWS_AS(orders.on_accepted(t1, 0, Side::Buy, 100.0, 10), std::logic_error);
    CHECK_THROWS_AS(orders.on_accepted(t2, 0, Side::Buy, 100.0, 0), std::invalid_argument);
    CHECK_THROWS_AS(orders.on_accepted(t2, 0, Side::Buy, 100.0, -3), std::invalid_argument);
    CHECK_THROWS_AS(orders.on_arrived(t2, mm::OrderId{1}), std::out_of_range);
    CHECK_THROWS_AS(orders.on_filled(t2, 1), std::out_of_range);

    orders.on_arrived(t1, mm::OrderId{1});
    CHECK_THROWS_AS(orders.on_arrived(t1, mm::OrderId{2}), std::logic_error); // already there
    CHECK_THROWS_AS(orders.on_filled(t1, 0), std::invalid_argument);
    CHECK_THROWS_AS(orders.on_filled(t1, -1), std::invalid_argument);
    CHECK_THROWS_AS(orders.on_filled(t1, 11), std::invalid_argument);

    // None of that changed the order.
    REQUIRE(orders.size() == 1);
    CHECK(orders.orders()[0].remaining == 10);
    CHECK(orders.orders()[0].id == mm::OrderId{1});
}

TEST_CASE("The span is the list itself, so the agent sees what the loop has recorded",
          "[simulation][agent_orders]") {
    sim::AgentOrders orders;
    orders.on_accepted(t1, 0, Side::Buy, 100.0, 10);
    orders.on_accepted(t2, 1, Side::Sell, 50.0, 20);

    const auto view = orders.orders();

    CHECK(view.size() == 2);
    CHECK(view.data() == orders.orders().data());
}
