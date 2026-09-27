/// \file
/// End-to-end tests for MatchingEngine::submit() across the 5 valid
/// kind/TIF combinations (Limit+GTC, Limit+IOC, Limit+FOK, Market+IOC,
/// Market+FOK), MatchingEngineObserver notification, and cancel(). The
/// underlying matching loop mechanics (price/time priority, multi-level
/// walks) are already covered by match_against_book.cpp and not re-tested
/// here - these tests focus on MatchingEngine's own responsibility: which
/// order kinds rest vs. discard their remainder, the Fill-Or-Kill
/// feasibility gate, and observer wiring.

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <core/matching_engine.hpp>
#include <core/order.hpp>
#include <core/order_id.hpp>
#include <core/side.hpp>
#include <core/trade.hpp>

using mms::core::LimitOrder;
using mms::core::MarketOrder;
using mms::core::MarketTimeInForce;
using mms::core::MatchingEngine;
using mms::core::MatchingEngineObserver;
using mms::core::Order;
using mms::core::OrderId;
using mms::core::Side;
using mms::core::TimeInForce;
using mms::core::Trade;

namespace {

struct RecordingObserver : MatchingEngineObserver<double, double> {
    std::vector<Trade<double, double>> trades;
    std::vector<OrderId> added_ids;
    std::vector<OrderId> cancelled_ids;

    void on_trade(const Trade<double, double>& trade) override { trades.push_back(trade); }

    void on_order_added(OrderId id, const LimitOrder<double, double>&) override {
        added_ids.push_back(id);
    }

    void on_order_cancelled(OrderId id) override { cancelled_ids.push_back(id); }
};

Order<double, double> limit(Side side, double price, double quantity, TimeInForce tif) {
    return LimitOrder<double, double>{
        .side = side, .price = price, .quantity = quantity, .time_in_force = tif};
}

Order<double, double> market(Side side, double quantity, MarketTimeInForce tif) {
    return MarketOrder<double>{.side = side, .quantity = quantity, .time_in_force = tif};
}

} // namespace

TEST_CASE("Limit+GTC with nothing to match against rests entirely and notifies on_order_added",
          "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    const OrderId id = engine.submit(limit(Side::Buy, 100.0, 10.0, TimeInForce::GTC));

    CHECK(observer.trades.empty());
    REQUIRE(observer.added_ids.size() == 1);
    CHECK(observer.added_ids[0] == id);
    CHECK(engine.book().depth_at(Side::Buy, 100.0) == 10.0);
}

TEST_CASE("Limit+GTC rests only its unfilled remainder after matching", "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    engine.submit(limit(Side::Sell, 100.0, 4.0, TimeInForce::GTC)); // resting liquidity
    const OrderId id = engine.submit(limit(Side::Buy, 100.0, 10.0, TimeInForce::GTC));

    REQUIRE(observer.trades.size() == 1);
    CHECK(observer.trades[0].quantity == 4.0);
    REQUIRE(observer.added_ids.size() == 2); // the initial sell, then the buy's 6-unit remainder
    CHECK(observer.added_ids[1] == id);
    CHECK(engine.book().depth_at(Side::Buy, 100.0) == 6.0);
    CHECK(engine.book().empty(Side::Sell)); // fully consumed
}

TEST_CASE("Limit+IOC discards its unfilled remainder and never rests", "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    engine.submit(limit(Side::Sell, 100.0, 4.0, TimeInForce::GTC));
    engine.submit(limit(Side::Buy, 100.0, 10.0, TimeInForce::IOC));

    REQUIRE(observer.trades.size() == 1);
    CHECK(observer.trades[0].quantity == 4.0);
    CHECK(observer.added_ids.size() == 1); // only the original resting sell, never the IOC
    CHECK(engine.book().empty(Side::Buy));
}

TEST_CASE("Limit+FOK is rejected outright (zero fills) when not fully fillable now",
          "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    engine.submit(limit(Side::Sell, 100.0, 4.0, TimeInForce::GTC));
    engine.submit(limit(Side::Buy, 100.0, 10.0, TimeInForce::FOK)); // only 4 available, needs 10

    CHECK(observer.trades.empty()); // no partial fill, not even the 4 that were available
    CHECK(engine.book().depth_at(Side::Sell, 100.0) == 4.0); // untouched
    CHECK(engine.book().empty(Side::Buy));
}

TEST_CASE("Limit+FOK executes fully (never rests) when the book can fill it entirely",
          "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    engine.submit(limit(Side::Sell, 100.0, 10.0, TimeInForce::GTC));
    const OrderId id = engine.submit(limit(Side::Buy, 100.0, 10.0, TimeInForce::FOK));

    REQUIRE(observer.trades.size() == 1);
    CHECK(observer.trades[0].quantity == 10.0);
    CHECK(observer.trades[0].buy_order_id == id);
    CHECK(engine.book().empty(Side::Sell));
    CHECK(engine.book().empty(Side::Buy)); // FOK never rests, even when fully filled
    CHECK(observer.added_ids.size() == 1); // only the original resting sell
}

TEST_CASE("Limit+FOK with a SELL aggressor is rejected when not fully fillable now "
          "(the direction total_matchable's earlier bug broke)",
          "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    engine.submit(limit(Side::Buy, 100.0, 4.0, TimeInForce::GTC)); // only 4 available to sell into
    engine.submit(limit(Side::Sell, 100.0, 10.0, TimeInForce::FOK)); // needs 10

    CHECK(observer.trades.empty());
    CHECK(engine.book().depth_at(Side::Buy, 100.0) == 4.0); // untouched
}

TEST_CASE("Limit+FOK with a SELL aggressor executes fully when the book can fill it",
          "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    engine.submit(limit(Side::Buy, 100.0, 10.0, TimeInForce::GTC));
    const OrderId id = engine.submit(limit(Side::Sell, 100.0, 10.0, TimeInForce::FOK));

    REQUIRE(observer.trades.size() == 1);
    CHECK(observer.trades[0].quantity == 10.0);
    CHECK(observer.trades[0].sell_order_id == id);
    CHECK(engine.book().empty(Side::Buy));
}

TEST_CASE("Market+IOC matches at any price and discards any unfilled remainder",
          "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    engine.submit(limit(Side::Sell, 105.0, 3.0, TimeInForce::GTC));
    engine.submit(market(Side::Buy, 10.0, MarketTimeInForce::IOC)); // no price limit

    REQUIRE(observer.trades.size() == 1);
    CHECK(observer.trades[0].price == 105.0); // the maker's price, market order has none
    CHECK(observer.trades[0].quantity == 3.0);
    CHECK(engine.book().empty(Side::Sell));
    CHECK(observer.added_ids.size() == 1); // only the initial resting sell; the Market order itself
                                           // never rests, so it never adds a second entry
}

TEST_CASE("Market+FOK is rejected when the book can't fill it entirely",
          "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    engine.submit(limit(Side::Sell, 105.0, 3.0, TimeInForce::GTC));
    engine.submit(market(Side::Buy, 10.0, MarketTimeInForce::FOK)); // only 3 available, needs 10

    CHECK(observer.trades.empty());
    CHECK(engine.book().depth_at(Side::Sell, 105.0) == 3.0); // untouched
}

TEST_CASE("Market+FOK executes fully when the book can fill it entirely",
          "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    engine.submit(limit(Side::Sell, 105.0, 10.0, TimeInForce::GTC));
    engine.submit(market(Side::Buy, 10.0, MarketTimeInForce::FOK));

    REQUIRE(observer.trades.size() == 1);
    CHECK(observer.trades[0].quantity == 10.0);
    CHECK(engine.book().empty(Side::Sell));
}

TEST_CASE("MatchingEngine::cancel removes a resting order and notifies on_order_cancelled",
          "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    const OrderId id = engine.submit(limit(Side::Buy, 100.0, 10.0, TimeInForce::GTC));
    const bool cancelled = engine.cancel(id);

    CHECK(cancelled);
    REQUIRE(observer.cancelled_ids.size() == 1);
    CHECK(observer.cancelled_ids[0] == id);
    CHECK(engine.book().empty(Side::Buy));
}

TEST_CASE("MatchingEngine::cancel of an unknown id returns false and notifies nothing",
          "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver observer;
    engine.add_observer(observer);

    CHECK_FALSE(engine.cancel(OrderId{999}));
    CHECK(observer.cancelled_ids.empty());
}

TEST_CASE("MatchingEngine notifies every registered observer", "[core][matching_engine]") {
    MatchingEngine<double, double> engine;
    RecordingObserver first;
    RecordingObserver second;
    engine.add_observer(first);
    engine.add_observer(second);

    engine.submit(limit(Side::Sell, 100.0, 5.0, TimeInForce::GTC));
    engine.submit(limit(Side::Buy, 100.0, 5.0, TimeInForce::IOC));

    CHECK(first.trades.size() == 1);
    CHECK(second.trades.size() == 1);
}

TEST_CASE("MatchingEngine::submit assigns distinct, increasing ids", "[core][matching_engine]") {
    MatchingEngine<double, double> engine;

    const OrderId first = engine.submit(limit(Side::Buy, 99.0, 1.0, TimeInForce::GTC));
    const OrderId second = engine.submit(limit(Side::Buy, 98.0, 1.0, TimeInForce::GTC));

    CHECK(first != second);
}
