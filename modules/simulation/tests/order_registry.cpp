/// \file
/// Tests for simulation/order_registry.hpp. An order id means something only within one
/// asset's engine, so the registry must tell apart "order 1 of asset 0" and "order 1 of
/// asset 1"; it must refuse to record an order twice, and let an order go whichever way it
/// ended.

#include <optional>
#include <stdexcept>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>
#include <mm/types.hpp>

#include <simulation/account.hpp>
#include <simulation/agent_id.hpp>
#include <simulation/order_registry.hpp>

namespace {

namespace sim = mms::simulation;
namespace mm = mms::mm;

sim::OrderOwner owner(sim::AgentId agent, std::uint64_t ticket) {
    return sim::OrderOwner{.agent = agent, .ticket = sim::Ticket{ticket}};
}

} // namespace

static_assert(std::is_same_v<sim::AgentId, std::size_t>);

TEST_CASE("An empty registry knows no order", "[simulation][order_registry]") {
    const sim::OrderRegistry registry;

    CHECK(registry.size() == 0);
    CHECK_FALSE(registry.find(0, mm::OrderId{1}).has_value());
}

TEST_CASE("A recorded order is found with its owner", "[simulation][order_registry]") {
    sim::OrderRegistry registry;

    registry.add(2, mm::OrderId{7}, owner(3, 11));

    REQUIRE(registry.find(2, mm::OrderId{7}).has_value());
    CHECK(*registry.find(2, mm::OrderId{7}) == owner(3, 11));
    CHECK(registry.find(2, mm::OrderId{7})->agent == 3);
    CHECK(registry.find(2, mm::OrderId{7})->ticket == sim::Ticket{11});
    CHECK(registry.size() == 1);
}

TEST_CASE("The same order id on two assets is two different orders",
          "[simulation][order_registry]") {
    sim::OrderRegistry registry;

    registry.add(0, mm::OrderId{1}, owner(0, 1));
    registry.add(1, mm::OrderId{1}, owner(5, 2));

    CHECK(registry.find(0, mm::OrderId{1})->agent == 0);
    CHECK(registry.find(1, mm::OrderId{1})->agent == 5);
    CHECK_FALSE(registry.find(2, mm::OrderId{1}).has_value());
    CHECK(registry.size() == 2);
}

TEST_CASE("Different ids on one asset are different orders", "[simulation][order_registry]") {
    sim::OrderRegistry registry;

    registry.add(0, mm::OrderId{1}, owner(0, 1));
    registry.add(0, mm::OrderId{2}, owner(1, 2));

    CHECK(registry.find(0, mm::OrderId{1})->agent == 0);
    CHECK(registry.find(0, mm::OrderId{2})->agent == 1);
    CHECK_FALSE(registry.find(0, mm::OrderId{3}).has_value());
}

TEST_CASE("Recording an order twice is refused and keeps the first owner",
          "[simulation][order_registry]") {
    sim::OrderRegistry registry;
    registry.add(0, mm::OrderId{1}, owner(0, 1));

    CHECK_THROWS_AS(registry.add(0, mm::OrderId{1}, owner(9, 9)), std::logic_error);

    CHECK(*registry.find(0, mm::OrderId{1}) == owner(0, 1));
    CHECK(registry.size() == 1);
}

TEST_CASE("Removing an order forgets it, and only it", "[simulation][order_registry]") {
    sim::OrderRegistry registry;
    registry.add(0, mm::OrderId{1}, owner(0, 1));
    registry.add(0, mm::OrderId{2}, owner(0, 2));
    registry.add(1, mm::OrderId{1}, owner(0, 3));

    CHECK(registry.remove(0, mm::OrderId{1}));

    CHECK_FALSE(registry.find(0, mm::OrderId{1}).has_value());
    CHECK(registry.find(0, mm::OrderId{2}).has_value());
    CHECK(registry.find(1, mm::OrderId{1}).has_value());
    CHECK(registry.size() == 2);
}

TEST_CASE("Removing an order that is not there reports false", "[simulation][order_registry]") {
    sim::OrderRegistry registry;
    registry.add(0, mm::OrderId{1}, owner(0, 1));

    CHECK_FALSE(registry.remove(0, mm::OrderId{2}));
    CHECK_FALSE(registry.remove(1, mm::OrderId{1}));
    CHECK(registry.remove(0, mm::OrderId{1}));
    CHECK_FALSE(registry.remove(0, mm::OrderId{1})); // already gone

    CHECK(registry.size() == 0);
}

TEST_CASE("A removed order can be recorded again", "[simulation][order_registry]") {
    sim::OrderRegistry registry;
    registry.add(0, mm::OrderId{1}, owner(0, 1));
    static_cast<void>(registry.remove(0, mm::OrderId{1}));

    CHECK_NOTHROW(registry.add(0, mm::OrderId{1}, owner(4, 8)));

    CHECK(*registry.find(0, mm::OrderId{1}) == owner(4, 8));
}

TEST_CASE("Owners compare field by field", "[simulation][order_registry]") {
    CHECK(owner(1, 2) == owner(1, 2));
    CHECK_FALSE(owner(1, 2) == owner(2, 2));
    CHECK_FALSE(owner(1, 2) == owner(1, 3));
}
