/// \file
/// Tests for agents/agent_view.hpp. The view is a bundle of non-owning spans and
/// small plain structs: it can be copied freely, a default one shows nothing, and
/// the spans see exactly the buffers they were built over, in order. The signal is
/// empty unless somebody sets it, because an agent that is not informed must not
/// see the true price.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

#include <agents/agent_view.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

namespace agents = mms::agents;
namespace mm = mms::mm;

} // namespace

static_assert(std::is_trivially_copyable_v<agents::BookLevel>);
static_assert(std::is_trivially_copyable_v<agents::TradePrint>);
static_assert(std::is_trivially_copyable_v<agents::OpenOrder>);
static_assert(std::is_trivially_copyable_v<agents::AssetView>);
static_assert(std::is_trivially_copyable_v<agents::AccountView>);
static_assert(std::is_trivially_copyable_v<agents::AgentView>);

TEST_CASE("The small records keep what they are given", "[agents][agent_view]") {
    constexpr agents::BookLevel level{.price = 99.5, .quantity = 120};
    constexpr agents::TradePrint print{.price = 100.0, .quantity = 7, .aggressor = mm::Side::Sell};
    constexpr agents::OpenOrder order{
        .asset = 4, .id = mm::OrderId{12}, .side = mm::Side::Buy, .price = 98.75, .remaining = 30};

    CHECK(level.price == 99.5);
    CHECK(level.quantity == 120);
    CHECK(print.price == 100.0);
    CHECK(print.quantity == 7);
    CHECK(print.aggressor == mm::Side::Sell);
    CHECK(order.asset == 4);
    CHECK(order.id == mm::OrderId{12});
    CHECK(order.side == mm::Side::Buy);
    CHECK(order.price == 98.75);
    CHECK(order.remaining == 30);
}

TEST_CASE("An open order is resting unless it says otherwise", "[agents][agent_view]") {
    constexpr agents::OpenOrder resting{
        .asset = 0, .id = mm::OrderId{1}, .side = mm::Side::Sell, .price = 1.0, .remaining = 1};
    constexpr agents::OpenOrder on_its_way{.asset = 0,
                                           .id = mm::OrderId{0},
                                           .side = mm::Side::Sell,
                                           .price = 1.0,
                                           .remaining = 1,
                                           .state = agents::OrderState::Submitting};
    constexpr agents::OpenOrder being_cancelled{.asset = 0,
                                                .id = mm::OrderId{2},
                                                .side = mm::Side::Buy,
                                                .price = 1.0,
                                                .remaining = 1,
                                                .state = agents::OrderState::Cancelling};

    CHECK(resting.state == agents::OrderState::Resting);
    CHECK(on_its_way.state == agents::OrderState::Submitting);
    CHECK(being_cancelled.state == agents::OrderState::Cancelling);
}

TEST_CASE("The three order states are distinct and numbered from zero", "[agents][agent_view]") {
    STATIC_REQUIRE(static_cast<int>(agents::OrderState::Submitting) == 0);
    STATIC_REQUIRE(static_cast<int>(agents::OrderState::Resting) == 1);
    STATIC_REQUIRE(static_cast<int>(agents::OrderState::Cancelling) == 2);
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<agents::OrderState>, std::uint8_t>);
}

TEST_CASE("A default asset view shows nothing, and in particular no signal",
          "[agents][agent_view]") {
    const agents::AssetView view{};

    CHECK(view.bids.empty());
    CHECK(view.asks.empty());
    CHECK(view.recent_trades.empty());
    CHECK_FALSE(view.signal.has_value());
}

TEST_CASE("An asset view carries the tick size of its asset, one cent unless set",
          "[agents][agent_view]") {
    const agents::AssetView standard{};
    const agents::AssetView coarse{
        .bids = {}, .asks = {}, .recent_trades = {}, .signal = std::nullopt, .tick_size = 0.5};

    CHECK(standard.tick_size == 0.01);
    CHECK(coarse.tick_size == 0.5);
}

TEST_CASE("An agent view shows no factors unless it is given some", "[agents][agent_view]") {
    const std::array<double, 2> moves{0.5, -0.25};
    const agents::AgentView plain{.tick = 0,
                                  .time = 0.0,
                                  .account = {.cash = 1.0, .buying_power = 1.0, .positions = {}},
                                  .open_orders = {},
                                  .assets = {}};
    const agents::AgentView informed{.tick = 0,
                                     .time = 0.0,
                                     .account = {.cash = 1.0, .buying_power = 1.0, .positions = {}},
                                     .open_orders = {},
                                     .assets = {},
                                     .factors = moves};

    CHECK(plain.factors.empty());
    REQUIRE(informed.factors.size() == 2);
    CHECK(informed.factors[0] == 0.5);
    CHECK(informed.factors[1] == -0.25);
    CHECK(informed.factors.data() == moves.data()); // a view of the caller's memory
}

TEST_CASE("An asset view sees its buffers as they are, best level first", "[agents][agent_view]") {
    const std::array<agents::BookLevel, 3> bids{{{99.0, 10}, {98.0, 20}, {97.0, 30}}};
    const std::array<agents::BookLevel, 2> asks{{{101.0, 5}, {102.0, 15}}};
    const std::array<agents::TradePrint, 2> tape{
        {{100.0, 3, mm::Side::Buy}, {100.5, 4, mm::Side::Sell}}};

    const agents::AssetView view{
        .bids = bids, .asks = asks, .recent_trades = tape, .signal = 100.2};

    REQUIRE(view.bids.size() == 3);
    REQUIRE(view.asks.size() == 2);
    REQUIRE(view.recent_trades.size() == 2);
    CHECK(view.bids.front().price == 99.0);
    CHECK(view.bids.back().price == 97.0);
    CHECK(view.asks.front().price == 101.0);
    CHECK(view.recent_trades.back().aggressor == mm::Side::Sell);
    // The spans look at the caller's memory, they do not copy it.
    CHECK(view.bids.data() == bids.data());
    CHECK(view.asks.data() == asks.data());
    REQUIRE(view.signal.has_value());
    CHECK(*view.signal == 100.2);
}

TEST_CASE("An account view exposes cash, buying power and one signed position per asset",
          "[agents][agent_view]") {
    const std::array<mm::Quantity, 3> positions{10, -25, 0};
    const agents::AccountView account{
        .cash = 1000.0, .buying_power = 400.0, .positions = positions};

    CHECK(account.cash == 1000.0);
    CHECK(account.buying_power == 400.0);
    REQUIRE(account.positions.size() == 3);
    CHECK(account.positions[0] == 10);
    CHECK(account.positions[1] == -25);
    CHECK(account.positions[2] == 0);
}

TEST_CASE("An agent view ties the tick, the account, the open orders and the assets together",
          "[agents][agent_view]") {
    const std::array<mm::Quantity, 2> positions{5, -5};
    const std::array<agents::OpenOrder, 1> open{{{1, mm::OrderId{9}, mm::Side::Sell, 55.0, 12}}};
    const std::array<agents::AssetView, 2> assets{};

    const agents::AgentView view{
        .tick = 42,
        .time = 42.0,
        .account = {.cash = 10.0, .buying_power = 10.0, .positions = positions},
        .open_orders = open,
        .assets = assets};

    CHECK(view.tick == 42);
    CHECK(view.time == 42.0);
    CHECK(view.account.positions.size() == 2);
    REQUIRE(view.open_orders.size() == 1);
    CHECK(view.open_orders[0].id == mm::OrderId{9});
    CHECK(view.assets.size() == 2);
}

TEST_CASE("The tick is a 64-bit signed integer, the time a double", "[agents][agent_view]") {
    STATIC_REQUIRE(std::is_same_v<decltype(agents::AgentView::tick), std::int64_t>);
    STATIC_REQUIRE(std::is_same_v<decltype(agents::AgentView::time), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(agents::AgentView::elapsed), double>);
}

TEST_CASE("The time since the agent last acted defaults to zero and can be set",
          "[agents][agent_view]") {
    const agents::AgentView first{.tick = 0,
                                  .time = 0.0,
                                  .account = {.cash = 1.0, .buying_power = 1.0, .positions = {}},
                                  .open_orders = {},
                                  .assets = {}};
    const agents::AgentView later{.tick = 5,
                                  .time = 5.0,
                                  .elapsed = 5.0,
                                  .account = {.cash = 1.0, .buying_power = 1.0, .positions = {}},
                                  .open_orders = {},
                                  .assets = {}};

    CHECK(first.elapsed == 0.0);
    CHECK(later.elapsed == 5.0);
}

TEST_CASE("An agent view can be copied, and the copy looks at the same buffers",
          "[agents][agent_view]") {
    const std::array<mm::Quantity, 1> positions{3};
    const agents::AgentView original{
        .tick = 1,
        .time = 1.0,
        .account = {.cash = 5.0, .buying_power = 5.0, .positions = positions},
        .open_orders = {},
        .assets = {}};

    const agents::AgentView copy = original;

    CHECK(copy.tick == original.tick);
    CHECK(copy.account.positions.data() == original.account.positions.data());
}
