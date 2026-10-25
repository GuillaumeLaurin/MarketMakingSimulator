/// \file
/// Tests for agents/action.hpp. An action is plain data: it keeps what it is
/// given, the time-in-force defaults are the safe ones, and the variant holds
/// exactly the three alternatives in the documented order. The traits are
/// checked at compile time, since they are what lets an action be copied,
/// journalled and one day passed across a plugin boundary.

#include <cstddef>
#include <type_traits>
#include <variant>

#include <agents/action.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

namespace agents = mms::agents;
namespace mm = mms::mm;

} // namespace

static_assert(std::is_trivially_copyable_v<agents::SubmitLimit>);
static_assert(std::is_trivially_copyable_v<agents::SubmitMarket>);
static_assert(std::is_trivially_copyable_v<agents::Cancel>);
static_assert(std::is_trivially_copyable_v<agents::Action>);
static_assert(std::is_aggregate_v<agents::SubmitLimit>);
static_assert(std::is_aggregate_v<agents::SubmitMarket>);
static_assert(std::is_aggregate_v<agents::Cancel>);

static_assert(std::variant_size_v<agents::Action> == 3);
static_assert(std::is_same_v<std::variant_alternative_t<0, agents::Action>, agents::SubmitLimit>);
static_assert(std::is_same_v<std::variant_alternative_t<1, agents::Action>, agents::SubmitMarket>);
static_assert(std::is_same_v<std::variant_alternative_t<2, agents::Action>, agents::Cancel>);

TEST_CASE("A limit order keeps what it is given and defaults to good-till-cancelled",
          "[agents][action]") {
    constexpr agents::SubmitLimit order{
        .asset = 3, .side = mm::Side::Sell, .price = 101.25, .quantity = 40};

    CHECK(order.asset == 3);
    CHECK(order.side == mm::Side::Sell);
    CHECK(order.price == 101.25);
    CHECK(order.quantity == 40);
    CHECK(order.time_in_force == mm::TimeInForce::GTC);
}

TEST_CASE("A limit order can ask for another time-in-force", "[agents][action]") {
    constexpr agents::SubmitLimit order{.asset = 0,
                                        .side = mm::Side::Buy,
                                        .price = 10.0,
                                        .quantity = 1,
                                        .time_in_force = mm::TimeInForce::FOK};

    CHECK(order.time_in_force == mm::TimeInForce::FOK);
}

TEST_CASE("A market order keeps what it is given and defaults to immediate-or-cancel",
          "[agents][action]") {
    constexpr agents::SubmitMarket order{.asset = 1, .side = mm::Side::Buy, .quantity = 25};

    CHECK(order.asset == 1);
    CHECK(order.side == mm::Side::Buy);
    CHECK(order.quantity == 25);
    CHECK(order.time_in_force == mm::MarketTimeInForce::IOC);
}

TEST_CASE("A market order can ask for fill-or-kill", "[agents][action]") {
    constexpr agents::SubmitMarket order{.asset = 0,
                                         .side = mm::Side::Sell,
                                         .quantity = 5,
                                         .time_in_force = mm::MarketTimeInForce::FOK};

    CHECK(order.time_in_force == mm::MarketTimeInForce::FOK);
}

TEST_CASE("A cancel names the asset and the order", "[agents][action]") {
    constexpr agents::Cancel cancel{.asset = 2, .id = mm::OrderId{77}};

    CHECK(cancel.asset == 2);
    CHECK(cancel.id == mm::OrderId{77});
}

TEST_CASE("An Action holds whichever alternative it was built from", "[agents][action]") {
    const agents::Action limit =
        agents::SubmitLimit{.asset = 0, .side = mm::Side::Buy, .price = 9.5, .quantity = 3};
    const agents::Action market =
        agents::SubmitMarket{.asset = 1, .side = mm::Side::Sell, .quantity = 8};
    const agents::Action cancel = agents::Cancel{.asset = 2, .id = mm::OrderId{4}};

    REQUIRE(std::holds_alternative<agents::SubmitLimit>(limit));
    REQUIRE(std::holds_alternative<agents::SubmitMarket>(market));
    REQUIRE(std::holds_alternative<agents::Cancel>(cancel));
    CHECK(std::get<agents::SubmitLimit>(limit).price == 9.5);
    CHECK(std::get<agents::SubmitMarket>(market).quantity == 8);
    CHECK(std::get<agents::Cancel>(cancel).id == mm::OrderId{4});
}

TEST_CASE("Two equal actions are two copies of the same plain data", "[agents][action]") {
    const agents::Action original =
        agents::SubmitLimit{.asset = 0, .side = mm::Side::Buy, .price = 1.0, .quantity = 1};
    const agents::Action copy = original;

    REQUIRE(copy.index() == original.index());
    const auto& a = std::get<agents::SubmitLimit>(original);
    const auto& b = std::get<agents::SubmitLimit>(copy);
    CHECK(a.asset == b.asset);
    CHECK(a.side == b.side);
    CHECK(a.price == b.price);
    CHECK(a.quantity == b.quantity);
    CHECK(a.time_in_force == b.time_in_force);
}
