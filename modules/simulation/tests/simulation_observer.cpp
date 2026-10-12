/// \file
/// Tests for simulation/simulation_observer.hpp: every virtual is a true
/// no-op by default (so a observer overriding one method is never silently
/// forced to also handle the others), a recording subclass that overrides
/// only some methods receives exactly those calls with the right arguments,
/// and the base is usable non-owning and polymorphically, the way
/// World/Simulation will use it (several observers watching one run).

#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <agents/action.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/types.hpp>

#include <simulation/simulation_observer.hpp>

namespace sim = mms::simulation;
namespace agents = mms::agents;
namespace mm = mms::mm;

static_assert(!std::is_default_constructible_v<sim::SimulationObserver>,
              "SimulationObserver's constructor is protected to prevent slicing; "
              "only a subclass is constructible");

namespace {

/// Overrides every method and records what it was called with.
class RecordingObserver final : public sim::SimulationObserver {
public:
    struct TradeSeen {
        mm::AssetId asset;
        mm::Trade trade;
        sim::TradeParties parties;
    };

    struct RejectSeen {
        sim::AgentId agent;
        agents::Action action;
        agents::RejectReason reason;
    };

    std::vector<double> tick_starts;
    std::vector<double> tick_ends;
    std::vector<std::pair<mm::AssetId, mm::Price>> marks;
    std::vector<TradeSeen> trades;
    std::vector<RejectSeen> rejects;

    void on_tick_start(double time) override { tick_starts.push_back(time); }

    void on_tick_end(double time) override { tick_ends.push_back(time); }

    void on_mark(mm::AssetId asset, mm::Price mid) override { marks.emplace_back(asset, mid); }

    void
    on_trade(mm::AssetId asset, const mm::Trade& trade, const sim::TradeParties& parties) override {
        trades.push_back({.asset = asset, .trade = trade, .parties = parties});
    }

    void on_reject(sim::AgentId agent,
                   const agents::Action& action,
                   agents::RejectReason reason) override {
        rejects.push_back({.agent = agent, .action = action, .reason = reason});
    }
};

/// Overrides nothing: every call must be a silent no-op.
class SilentObserver final : public sim::SimulationObserver {};

mm::Trade make_trade(mm::Price price, mm::Quantity quantity) {
    return mm::Trade{.buy_order_id = mm::OrderId{1},
                     .sell_order_id = mm::OrderId{2},
                     .price = price,
                     .quantity = quantity};
}

} // namespace

TEST_CASE("A subclass overriding nothing behaves as a pure no-op (the base itself is not "
          "constructible on its own: its constructor is protected to prevent slicing)",
          "[simulation][observer]") {
    SilentObserver observer;

    observer.on_tick_start(1.0);
    observer.on_tick_end(1.0);
    observer.on_mark(0, 100.0);
    observer.on_trade(0,
                      make_trade(100.0, 5),
                      sim::TradeParties{.buyer = 0, .seller = 1, .aggressor = mm::Side::Buy});
    observer.on_reject(
        0, agents::Cancel{.asset = 0, .id = mm::OrderId{1}}, agents::RejectReason::InvalidOrder);

    SUCCEED();
}

TEST_CASE("A recording observer receives on_tick_start with the right time",
          "[simulation][observer]") {
    RecordingObserver observer;

    observer.on_tick_start(0.0);
    observer.on_tick_start(0.5);
    observer.on_tick_start(1.0);

    CHECK(observer.tick_starts == std::vector<double>{0.0, 0.5, 1.0});
}

TEST_CASE("A recording observer receives on_tick_end with the right time, after on_tick_start",
          "[simulation][observer]") {
    RecordingObserver observer;

    observer.on_tick_start(0.5);
    observer.on_tick_end(0.5);
    observer.on_tick_start(1.0);
    observer.on_tick_end(1.0);

    CHECK(observer.tick_starts == std::vector<double>{0.5, 1.0});
    CHECK(observer.tick_ends == std::vector<double>{0.5, 1.0});
}

TEST_CASE("A recording observer receives on_mark with the asset and the mid",
          "[simulation][observer]") {
    RecordingObserver observer;

    observer.on_mark(0, 100.0);
    observer.on_mark(1, 50.0);

    REQUIRE(observer.marks.size() == 2);
    CHECK(observer.marks[0] == std::pair<mm::AssetId, mm::Price>{0, 100.0});
    CHECK(observer.marks[1] == std::pair<mm::AssetId, mm::Price>{1, 50.0});
}

TEST_CASE("A recording observer receives on_trade with the asset, the trade and the two agents",
          "[simulation][observer]") {
    RecordingObserver observer;

    observer.on_trade(0,
                      make_trade(100.0, 7),
                      sim::TradeParties{.buyer = 2, .seller = 5, .aggressor = mm::Side::Buy});
    observer.on_trade(1,
                      make_trade(50.0, 3),
                      sim::TradeParties{.buyer = 4, .seller = 1, .aggressor = mm::Side::Sell});

    REQUIRE(observer.trades.size() == 2);
    CHECK(observer.trades[0].asset == 0);
    CHECK(observer.trades[0].trade.quantity == 7);
    CHECK(observer.trades[0].parties.buyer == 2);
    CHECK(observer.trades[0].parties.seller == 5);
    CHECK(observer.trades[1].parties.aggressor == mm::Side::Sell);
}

TEST_CASE("The aggressive agent is the buyer when a buyer started the trade, the seller otherwise",
          "[simulation][observer]") {
    const sim::TradeParties buyer_took{.buyer = 2, .seller = 5, .aggressor = mm::Side::Buy};
    CHECK(buyer_took.aggressive() == 2);
    CHECK(buyer_took.passive() == 5);

    const sim::TradeParties seller_took{.buyer = 2, .seller = 5, .aggressor = mm::Side::Sell};
    CHECK(seller_took.aggressive() == 5);
    CHECK(seller_took.passive() == 2);
}

TEST_CASE("A recording observer receives on_reject with the agent, the action and the reason",
          "[simulation][observer]") {
    RecordingObserver observer;

    observer.on_reject(
        3,
        agents::SubmitLimit{.asset = 1, .side = mm::Side::Buy, .price = 9.0, .quantity = 4},
        agents::RejectReason::InsufficientCash);

    REQUIRE(observer.rejects.size() == 1);
    CHECK(observer.rejects[0].agent == 3);
    CHECK(observer.rejects[0].reason == agents::RejectReason::InsufficientCash);
    CHECK(std::get<agents::SubmitLimit>(observer.rejects[0].action).quantity == 4);
}

TEST_CASE("Several observers can watch the same calls independently", "[simulation][observer]") {
    RecordingObserver first;
    RecordingObserver second;
    const std::vector<sim::SimulationObserver*> observers{&first, &second};

    for (sim::SimulationObserver* observer : observers) {
        observer->on_mark(0, 123.0);
    }

    CHECK(first.marks.size() == 1);
    CHECK(second.marks.size() == 1);
}

TEST_CASE("A RecordingObserver is usable, and destroyed, through the base class pointer",
          "[simulation][observer]") {
    const std::unique_ptr<sim::SimulationObserver> observer = std::make_unique<RecordingObserver>();

    observer->on_tick_start(2.0);

    CHECK(dynamic_cast<RecordingObserver&>(*observer).tick_starts == std::vector<double>{2.0});
}
