/// \file
/// Tests for simulation/simulation_observer.hpp: every virtual is a true
/// no-op by default (so a observer overriding one method is never silently
/// forced to also handle the others), a recording subclass that overrides
/// only some methods receives exactly those calls with the right arguments,
/// and the base is usable non-owning and polymorphically, the way
/// World/Simulation will use it (several observers watching one run).

#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mm/quote.hpp>
#include <mm/types.hpp>

#include <simulation/simulation_observer.hpp>

namespace sim = mms::simulation;
namespace mm = mms::mm;

static_assert(!std::is_default_constructible_v<sim::SimulationObserver>,
              "SimulationObserver's constructor is protected to prevent slicing; "
              "only a subclass is constructible");

namespace {

/// Overrides every method and records what it was called with.
class RecordingObserver final : public sim::SimulationObserver {
public:
    std::vector<double> tick_starts;
    std::vector<double> tick_ends;
    std::vector<std::pair<mm::AssetId, mm::Price>> marks;
    std::vector<std::pair<mm::AssetId, mm::AssetQuotes>> quotes;
    std::vector<std::tuple<mm::AssetId, mm::Trade, bool>> trades;

    void on_tick_start(double time) override { tick_starts.push_back(time); }

    void on_tick_end(double time) override { tick_ends.push_back(time); }

    void on_mark(mm::AssetId asset, mm::Price mid) override { marks.emplace_back(asset, mid); }

    void on_quote(mm::AssetId asset, const mm::AssetQuotes& asset_quotes) override {
        quotes.emplace_back(asset, asset_quotes);
    }

    void on_trade(mm::AssetId asset, const mm::Trade& trade, bool aggressor_is_mm) override {
        trades.emplace_back(asset, trade, aggressor_is_mm);
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
    observer.on_quote(0, mm::AssetQuotes{});
    observer.on_trade(0, make_trade(100.0, 5), false);

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

TEST_CASE("A recording observer receives on_quote with the live quotes, including an empty side",
          "[simulation][observer]") {
    RecordingObserver observer;
    const mm::AssetQuotes asked_both{.bid = mm::Quote{.price = 99.0, .quantity = 5},
                                     .ask = mm::Quote{.price = 101.0, .quantity = 5}};
    const mm::AssetQuotes asked_ask_only{.bid = std::nullopt,
                                         .ask = mm::Quote{.price = 101.0, .quantity = 5}};

    observer.on_quote(0, asked_both);
    observer.on_quote(0, asked_ask_only);

    REQUIRE(observer.quotes.size() == 2);
    CHECK(observer.quotes[0].second.bid.has_value());
    CHECK_FALSE(observer.quotes[1].second.bid.has_value());
    CHECK(observer.quotes[1].second.ask.has_value());
}

TEST_CASE("A recording observer receives on_trade with the asset, the trade and the aggressor flag",
          "[simulation][observer]") {
    RecordingObserver observer;

    observer.on_trade(0, make_trade(100.0, 7), true);
    observer.on_trade(1, make_trade(50.0, 3), false);

    REQUIRE(observer.trades.size() == 2);
    CHECK(std::get<0>(observer.trades[0]) == 0);
    CHECK(std::get<1>(observer.trades[0]).quantity == 7);
    CHECK(std::get<2>(observer.trades[0]) == true);
    CHECK(std::get<2>(observer.trades[1]) == false);
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
