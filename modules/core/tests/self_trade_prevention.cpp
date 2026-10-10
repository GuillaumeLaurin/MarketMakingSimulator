/// \file
/// Tests for self-trade prevention, from the bottom up: PriceLevels and OrderBook
/// can leave named orders out of a matchable total, match_against_book cancels the
/// resting orders an `is_own` test names instead of trading with them, and
/// MatchingEngine wires it all to a test the caller sets. The policy is "cancel the
/// oldest": the resting own order goes, the incoming order carries on. Without a
/// test set, behaviour is exactly what it was.

#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <core/matching_engine.hpp>
#include <core/order.hpp>
#include <core/order_book.hpp>
#include <core/order_id.hpp>
#include <core/side.hpp>
#include <core/trade.hpp>

using mms::core::crosses;
using mms::core::LimitOrder;
using mms::core::MarketOrder;
using mms::core::MarketTimeInForce;
using mms::core::match_against_book;
using mms::core::MatchingEngine;
using mms::core::MatchingEngineObserver;
using mms::core::Order;
using mms::core::OrderBook;
using mms::core::OrderId;
using mms::core::PriceLevels;
using mms::core::Side;
using mms::core::TimeInForce;
using mms::core::Trade;

namespace {

LimitOrder<double, double> resting(Side side, double price, double quantity) {
    return LimitOrder<double, double>{
        .side = side, .price = price, .quantity = quantity, .time_in_force = TimeInForce::GTC};
}

auto buy_accepts(double buy_price) {
    return
        [buy_price](double resting_price) { return crosses(Side::Buy, buy_price, resting_price); };
}

auto owned_by(OrderId first, OrderId second = OrderId{0}) {
    return [first, second](OrderId id) { return id == first || id == second; };
}

Order<double, double> limit(Side side, double price, double quantity, TimeInForce tif) {
    return LimitOrder<double, double>{
        .side = side, .price = price, .quantity = quantity, .time_in_force = tif};
}

Order<double, double> market(Side side, double quantity, MarketTimeInForce tif) {
    return MarketOrder<double>{.side = side, .quantity = quantity, .time_in_force = tif};
}

// Logs what an engine reports, in the order it reports it.
struct EventLog : MatchingEngineObserver<double, double> {
    std::vector<std::string> events;
    std::vector<OrderId> cancelled;
    std::vector<Trade<double, double>> trades;

    void on_order_added(OrderId id, const LimitOrder<double, double>& /*order*/) override {
        events.push_back("added " + std::to_string(static_cast<int>(id)));
    }

    void on_order_cancelled(OrderId id) override {
        events.push_back("cancelled " + std::to_string(static_cast<int>(id)));
        cancelled.push_back(id);
    }

    void on_trade(const Trade<double, double>& trade) override {
        events.push_back("trade");
        trades.push_back(trade);
    }
};

// An engine whose resting orders are "own" when their id is in `own`.
struct Fixture {
    MatchingEngine<double, double> engine;
    EventLog log;
    std::set<OrderId> own;

    Fixture() {
        engine.add_observer(log);
        engine.set_self_trade_prevention([this](OrderId id) { return own.contains(id); });
    }

    // Rests a limit order and, if `mine`, marks it as own.
    OrderId rest(Side side, double price, double quantity, bool mine) {
        const OrderId id = engine.submit(limit(side, price, quantity, TimeInForce::GTC));
        if (mine) {
            own.insert(id);
        }
        return id;
    }
};

} // namespace

// ------------------------------------------------------------------ PriceLevels

TEST_CASE("PriceLevels::total_matchable_* leave out the entries an exclusion names",
          "[core][self_trade]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, resting(Side::Buy, 100.0, 3.0));
    levels.add(OrderId{2}, resting(Side::Buy, 100.0, 4.0));
    levels.add(OrderId{3}, resting(Side::Buy, 101.0, 5.0));
    levels.add(OrderId{4}, resting(Side::Buy, 102.0, 100.0));
    const auto accepts_low = [](double price) { return price <= 101.0; };
    const auto accepts_high = [](double price) { return price >= 101.0; };
    const auto none = [](OrderId) { return false; };
    const auto second_and_third = [](OrderId id) { return id == OrderId{2} || id == OrderId{3}; };

    CHECK(levels.total_matchable_ascending(accepts_low, none) == 12.0);
    CHECK(levels.total_matchable_ascending(accepts_low, second_and_third) == 3.0);
    CHECK(levels.total_matchable_ascending(accepts_low) == 12.0);

    CHECK(levels.total_matchable_descending(accepts_high, none) == 105.0);
    CHECK(levels.total_matchable_descending(accepts_high, second_and_third) == 100.0);
    CHECK(levels.total_matchable_descending(accepts_high) == 105.0);
}

TEST_CASE("Excluding an entry does not move the price where the walk stops", "[core][self_trade]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, resting(Side::Sell, 100.0, 3.0));
    levels.add(OrderId{2}, resting(Side::Sell, 105.0, 9.0));
    const auto accepts = [](double price) { return price <= 101.0; };
    const auto first = [](OrderId id) { return id == OrderId{1}; };

    // The only entry inside the limit is excluded: nothing, and not the one beyond it.
    CHECK(levels.total_matchable_ascending(accepts, first) == 0.0);
}

// ------------------------------------------------------------------- OrderBook

TEST_CASE("OrderBook::total_matchable can leave named orders out, on either side",
          "[core][self_trade]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, resting(Side::Buy, 101.0, 5.0));
    book.add(OrderId{2}, resting(Side::Buy, 101.0, 7.0));
    book.add(OrderId{3}, resting(Side::Buy, 100.0, 3.0));
    book.add(OrderId{4}, resting(Side::Sell, 102.0, 4.0));
    book.add(OrderId{5}, resting(Side::Sell, 103.0, 6.0));
    const auto buy_side = [](double price) { return price >= 100.0; };
    const auto sell_side = [](double price) { return price <= 103.0; };
    const auto second = [](OrderId id) { return id == OrderId{2}; };
    const auto fifth = [](OrderId id) { return id == OrderId{5}; };

    CHECK(book.total_matchable(Side::Buy, buy_side) == 15.0);
    CHECK(book.total_matchable(Side::Buy, buy_side, second) == 8.0);
    CHECK(book.total_matchable(Side::Sell, sell_side) == 10.0);
    CHECK(book.total_matchable(Side::Sell, sell_side, fifth) == 4.0);
    // Counting leaves the book alone.
    CHECK(book.size(Side::Buy) == 3);
    CHECK(book.size(Side::Sell) == 2);
}

// ------------------------------------------------------------ match_against_book

TEST_CASE("Without an ownership test nothing is ever cancelled", "[core][self_trade]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, resting(Side::Sell, 100.0, 5.0));

    const auto result = match_against_book(book, OrderId{10}, Side::Buy, 5.0, buy_accepts(100.0));

    CHECK(result.self_cancelled.empty());
    CHECK(result.trades.size() == 1);
}

TEST_CASE("An own resting order is cancelled and the walk goes on to what is behind it",
          "[core][self_trade]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, resting(Side::Sell, 100.0, 5.0)); // own, first in line
    book.add(OrderId{2}, resting(Side::Sell, 100.0, 5.0)); // another participant's

    const auto result = match_against_book(
        book, OrderId{10}, Side::Buy, 5.0, buy_accepts(100.0), owned_by(OrderId{1}));

    REQUIRE(result.self_cancelled.size() == 1);
    CHECK(result.self_cancelled[0] == OrderId{1});
    REQUIRE(result.trades.size() == 1);
    CHECK(result.trades[0].sell_order_id == OrderId{2});
    CHECK(result.trades[0].quantity == 5.0);
    CHECK(result.remaining == 0.0);
    CHECK(book.empty(Side::Sell));
}

TEST_CASE("An own order behind another's at one price is cancelled only when reached",
          "[core][self_trade]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, resting(Side::Sell, 100.0, 3.0)); // another participant's, first
    book.add(OrderId{2}, resting(Side::Sell, 100.0, 5.0)); // own, behind it

    const auto result = match_against_book(
        book, OrderId{10}, Side::Buy, 10.0, buy_accepts(100.0), owned_by(OrderId{2}));

    REQUIRE(result.trades.size() == 1);
    CHECK(result.trades[0].sell_order_id == OrderId{1});
    CHECK(result.trades[0].quantity == 3.0);
    REQUIRE(result.self_cancelled.size() == 1);
    CHECK(result.self_cancelled[0] == OrderId{2});
    CHECK(result.remaining == 7.0);
}

TEST_CASE("An own order that is never reached is left alone", "[core][self_trade]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, resting(Side::Sell, 100.0, 5.0)); // another's, fills the aggressor
    book.add(OrderId{2}, resting(Side::Sell, 100.0, 5.0)); // own, behind it

    const auto result = match_against_book(
        book, OrderId{10}, Side::Buy, 5.0, buy_accepts(100.0), owned_by(OrderId{2}));

    CHECK(result.self_cancelled.empty());
    CHECK(book.size(Side::Sell) == 1);
    CHECK(book.depth_at(Side::Sell, 100.0) == 5.0);
}

TEST_CASE("An own order outside the price limit is left alone", "[core][self_trade]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, resting(Side::Sell, 100.0, 5.0));
    book.add(OrderId{2}, resting(Side::Sell, 105.0, 5.0)); // own, but too dear to cross

    const auto result = match_against_book(
        book, OrderId{10}, Side::Buy, 8.0, buy_accepts(100.0), owned_by(OrderId{2}));

    CHECK(result.self_cancelled.empty());
    CHECK(result.remaining == 3.0);
    CHECK(book.size(Side::Sell) == 1);
}

TEST_CASE("Several own orders are cancelled in the order they are met", "[core][self_trade]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, resting(Side::Sell, 100.0, 5.0)); // own
    book.add(OrderId{2}, resting(Side::Sell, 101.0, 5.0)); // own
    book.add(OrderId{3}, resting(Side::Sell, 102.0, 5.0)); // another's

    const auto result = match_against_book(
        book, OrderId{10}, Side::Buy, 5.0, buy_accepts(102.0), owned_by(OrderId{1}, OrderId{2}));

    REQUIRE(result.self_cancelled.size() == 2);
    CHECK(result.self_cancelled[0] == OrderId{1});
    CHECK(result.self_cancelled[1] == OrderId{2});
    REQUIRE(result.trades.size() == 1);
    CHECK(result.trades[0].sell_order_id == OrderId{3});
    CHECK(result.trades[0].price == 102.0);
}

TEST_CASE("When only own orders cross, nothing trades and the aggressor keeps its quantity",
          "[core][self_trade]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, resting(Side::Sell, 100.0, 5.0));

    const auto result = match_against_book(
        book, OrderId{10}, Side::Buy, 4.0, buy_accepts(100.0), owned_by(OrderId{1}));

    CHECK(result.trades.empty());
    CHECK(result.remaining == 4.0);
    CHECK(result.self_cancelled.size() == 1);
    CHECK(book.empty(Side::Sell));
}

TEST_CASE("A Sell aggressor does the same against own bids", "[core][self_trade]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, resting(Side::Buy, 100.0, 5.0)); // own
    book.add(OrderId{2}, resting(Side::Buy, 99.0, 5.0));  // another's

    const auto sell_accepts = [](double resting_price) {
        return crosses(Side::Sell, 99.0, resting_price);
    };
    const auto result =
        match_against_book(book, OrderId{10}, Side::Sell, 5.0, sell_accepts, owned_by(OrderId{1}));

    REQUIRE(result.self_cancelled.size() == 1);
    CHECK(result.self_cancelled[0] == OrderId{1});
    REQUIRE(result.trades.size() == 1);
    CHECK(result.trades[0].buy_order_id == OrderId{2});
    CHECK(result.trades[0].price == 99.0);
}

// --------------------------------------------------------------- MatchingEngine

TEST_CASE("Without self-trade prevention an engine trades with any resting order",
          "[core][self_trade]") {
    MatchingEngine<double, double> engine;
    EventLog log;
    engine.add_observer(log);
    engine.submit(limit(Side::Sell, 100.0, 5.0, TimeInForce::GTC));

    engine.submit(limit(Side::Buy, 100.0, 5.0, TimeInForce::GTC));

    CHECK(log.trades.size() == 1);
    CHECK(log.cancelled.empty());
}

TEST_CASE("A Limit+GTC buy that crosses an own ask cancels it and then rests",
          "[core][self_trade]") {
    Fixture f;
    const OrderId ask = f.rest(Side::Sell, 100.0, 5.0, true);

    const OrderId bid = f.engine.submit(limit(Side::Buy, 100.0, 4.0, TimeInForce::GTC));

    CHECK(f.log.trades.empty());
    REQUIRE(f.log.cancelled.size() == 1);
    CHECK(f.log.cancelled[0] == ask);
    CHECK(f.engine.book().empty(Side::Sell));
    REQUIRE(f.engine.book().best(Side::Buy).has_value());
    CHECK(f.engine.book().best(Side::Buy)->id == bid);
    CHECK(f.engine.book().best(Side::Buy)->order.quantity == 4.0);
}

TEST_CASE("A Limit+IOC buy that crosses only an own ask cancels it and is discarded",
          "[core][self_trade]") {
    Fixture f;
    f.rest(Side::Sell, 100.0, 5.0, true);

    f.engine.submit(limit(Side::Buy, 100.0, 4.0, TimeInForce::IOC));

    CHECK(f.log.trades.empty());
    CHECK(f.log.cancelled.size() == 1);
    CHECK(f.engine.book().empty(Side::Sell));
    CHECK(f.engine.book().empty(Side::Buy));
}

TEST_CASE("The order carries on to trade with another participant behind the own one",
          "[core][self_trade]") {
    Fixture f;
    const OrderId mine = f.rest(Side::Sell, 100.0, 5.0, true);
    const OrderId theirs = f.rest(Side::Sell, 101.0, 5.0, false);

    f.engine.submit(limit(Side::Buy, 101.0, 5.0, TimeInForce::GTC));

    REQUIRE(f.log.cancelled.size() == 1);
    CHECK(f.log.cancelled[0] == mine);
    REQUIRE(f.log.trades.size() == 1);
    CHECK(f.log.trades[0].sell_order_id == theirs);
    CHECK(f.log.trades[0].price == 101.0);
    CHECK(f.log.trades[0].quantity == 5.0);
    CHECK(f.engine.book().empty(Side::Sell));
    CHECK(f.engine.book().empty(Side::Buy));
}

TEST_CASE("A market order cancels an own ask in front of it and trades behind it",
          "[core][self_trade]") {
    Fixture f;
    const OrderId mine = f.rest(Side::Sell, 100.0, 5.0, true);
    const OrderId theirs = f.rest(Side::Sell, 102.0, 5.0, false);

    f.engine.submit(market(Side::Buy, 3.0, MarketTimeInForce::IOC));

    REQUIRE(f.log.cancelled.size() == 1);
    CHECK(f.log.cancelled[0] == mine);
    REQUIRE(f.log.trades.size() == 1);
    CHECK(f.log.trades[0].sell_order_id == theirs);
    CHECK(f.log.trades[0].quantity == 3.0);
}

TEST_CASE("Cancellations are reported before the trades of the same submission",
          "[core][self_trade]") {
    Fixture f;
    const OrderId mine = f.rest(Side::Sell, 100.0, 5.0, true);
    f.rest(Side::Sell, 101.0, 5.0, false);
    f.log.events.clear();

    f.engine.submit(limit(Side::Buy, 101.0, 5.0, TimeInForce::GTC));

    const std::vector<std::string> expected{"cancelled " + std::to_string(static_cast<int>(mine)),
                                            "trade"};
    CHECK(f.log.events == expected);
}

TEST_CASE("An own order outside the limit price is not touched", "[core][self_trade]") {
    Fixture f;
    f.rest(Side::Sell, 105.0, 5.0, true);

    f.engine.submit(limit(Side::Buy, 100.0, 4.0, TimeInForce::GTC));

    CHECK(f.log.cancelled.empty());
    CHECK(f.engine.book().size(Side::Sell) == 1);
    CHECK(f.engine.book().size(Side::Buy) == 1);
}

TEST_CASE("A cancelled own order is gone: cancelling it again fails", "[core][self_trade]") {
    Fixture f;
    const OrderId mine = f.rest(Side::Sell, 100.0, 5.0, true);

    f.engine.submit(limit(Side::Buy, 100.0, 1.0, TimeInForce::IOC));

    CHECK_FALSE(f.engine.cancel(mine));
}

TEST_CASE("Fill-or-kill ignores own liquidity: with only own orders it is rejected untouched",
          "[core][self_trade]") {
    Fixture f;
    const OrderId mine = f.rest(Side::Sell, 100.0, 5.0, true);

    f.engine.submit(limit(Side::Buy, 100.0, 5.0, TimeInForce::FOK));
    f.engine.submit(market(Side::Buy, 5.0, MarketTimeInForce::FOK));

    CHECK(f.log.trades.empty());
    CHECK(f.log.cancelled.empty());
    REQUIRE(f.engine.book().best(Side::Sell).has_value());
    CHECK(f.engine.book().best(Side::Sell)->id == mine);
}

TEST_CASE("Fill-or-kill is rejected when the others alone cannot fill it", "[core][self_trade]") {
    Fixture f;
    f.rest(Side::Sell, 100.0, 5.0, true);
    f.rest(Side::Sell, 100.0, 5.0, false);

    // 10 are on offer but only 5 of them are not the buyer's own.
    f.engine.submit(limit(Side::Buy, 100.0, 8.0, TimeInForce::FOK));

    CHECK(f.log.trades.empty());
    CHECK(f.log.cancelled.empty());
    CHECK(f.engine.book().size(Side::Sell) == 2);
}

TEST_CASE("Fill-or-kill succeeds on the others' liquidity and cancels the own order met on the way",
          "[core][self_trade]") {
    Fixture f;
    const OrderId mine = f.rest(Side::Sell, 100.0, 5.0, true);
    const OrderId theirs = f.rest(Side::Sell, 100.0, 5.0, false);

    f.engine.submit(limit(Side::Buy, 100.0, 5.0, TimeInForce::FOK));

    REQUIRE(f.log.cancelled.size() == 1);
    CHECK(f.log.cancelled[0] == mine);
    REQUIRE(f.log.trades.size() == 1);
    CHECK(f.log.trades[0].sell_order_id == theirs);
    CHECK(f.log.trades[0].quantity == 5.0);
}

TEST_CASE("Ownership is judged for whoever submits right now", "[core][self_trade]") {
    // Two participants, A and B. The test asks whether a resting order belongs to the
    // current submitter, so the same resting order is "own" for A and not for B.
    MatchingEngine<double, double> engine;
    EventLog log;
    engine.add_observer(log);
    std::set<OrderId> owned_by_a;
    bool a_is_submitting = true;
    engine.set_self_trade_prevention(
        [&](OrderId id) { return a_is_submitting == owned_by_a.contains(id); });

    a_is_submitting = true;
    const OrderId a_ask = engine.submit(limit(Side::Sell, 100.0, 5.0, TimeInForce::GTC));
    owned_by_a.insert(a_ask);

    a_is_submitting = false; // B buys: A's ask is someone else's, so they trade
    engine.submit(limit(Side::Buy, 100.0, 2.0, TimeInForce::GTC));
    CHECK(log.trades.size() == 1);
    CHECK(log.cancelled.empty());

    a_is_submitting = true; // A buys: its own ask, what is left of it, is cancelled
    engine.submit(limit(Side::Buy, 100.0, 2.0, TimeInForce::GTC));
    CHECK(log.trades.size() == 1);
    REQUIRE(log.cancelled.size() == 1);
    CHECK(log.cancelled[0] == a_ask);
}

TEST_CASE("An empty function turns self-trade prevention off again", "[core][self_trade]") {
    Fixture f;
    f.rest(Side::Sell, 100.0, 5.0, true);

    f.engine.set_self_trade_prevention({});
    f.engine.submit(limit(Side::Buy, 100.0, 5.0, TimeInForce::GTC));

    CHECK(f.log.trades.size() == 1);
    CHECK(f.log.cancelled.empty());
}

TEST_CASE("Own orders on the same side as the incoming order are never consulted",
          "[core][self_trade]") {
    Fixture f;
    const OrderId own_bid = f.rest(Side::Buy, 99.0, 5.0, true);

    f.engine.submit(limit(Side::Buy, 100.0, 5.0, TimeInForce::GTC));

    CHECK(f.log.cancelled.empty());
    CHECK(f.engine.book().size(Side::Buy) == 2);
    CHECK(f.engine.book().depth_at(Side::Buy, 99.0) == 5.0);
    static_cast<void>(own_bid);
}
