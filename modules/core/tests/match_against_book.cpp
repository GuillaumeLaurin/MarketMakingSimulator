/// \file
/// Tests for match_against_book(): the matching algorithm's core loop, in
/// complete isolation from MatchingEngine/MatchingEngineObserver/the
/// Fill-Or-Kill feasibility check - just an OrderBook built by hand and a
/// direct call.

#include <catch2/catch_test_macros.hpp>

#include <core/matching_engine.hpp>
#include <core/order.hpp>
#include <core/order_book.hpp>
#include <core/order_id.hpp>
#include <core/side.hpp>

using mms::core::crosses;
using mms::core::LimitOrder;
using mms::core::match_against_book;
using mms::core::OrderBook;
using mms::core::OrderId;
using mms::core::Side;
using mms::core::TimeInForce;

namespace {

LimitOrder<double, double> resting(Side side, double price, double quantity) {
    return LimitOrder<double, double>{
        .side = side, .price = price, .quantity = quantity, .time_in_force = TimeInForce::GTC};
}

/// The predicate a Buy aggressor at buy_price would use.
auto buy_accepts(double buy_price) {
    return
        [buy_price](double resting_price) { return crosses(Side::Buy, buy_price, resting_price); };
}

} // namespace

TEST_CASE("match_against_book against an empty book matches nothing",
          "[core][match_against_book]") {
    OrderBook<double, double> book;

    const auto result = match_against_book(book, OrderId{1}, Side::Buy, 10.0, buy_accepts(100.0));

    CHECK(result.trades.empty());
    CHECK(result.remaining == 10.0);
}

TEST_CASE("match_against_book fully fills the aggressor from a single resting entry",
          "[core][match_against_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{100}, resting(Side::Sell, 100.0, 20.0));

    const auto result = match_against_book(book, OrderId{1}, Side::Buy, 5.0, buy_accepts(100.0));

    REQUIRE(result.trades.size() == 1);
    CHECK(result.trades[0].price == 100.0);
    CHECK(result.trades[0].quantity == 5.0);
    CHECK(result.trades[0].buy_order_id == OrderId{1});
    CHECK(result.trades[0].sell_order_id == OrderId{100});
    CHECK(result.remaining == 0.0);
    CHECK(book.depth_at(Side::Sell, 100.0) == 15.0); // 20 - 5 left resting
}

TEST_CASE("match_against_book fully consumes a resting entry and reports the leftover remaining",
          "[core][match_against_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{100}, resting(Side::Sell, 100.0, 5.0));

    const auto result = match_against_book(book, OrderId{1}, Side::Buy, 8.0, buy_accepts(100.0));

    REQUIRE(result.trades.size() == 1);
    CHECK(result.trades[0].quantity == 5.0);
    CHECK(result.remaining == 3.0); // 8 requested, only 5 were available
    CHECK(book.empty(Side::Sell));  // the resting entry is gone
}

TEST_CASE("match_against_book walks multiple price levels in price priority",
          "[core][match_against_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{100}, resting(Side::Sell, 100.0, 3.0)); // best (cheapest) for the aggressor
    book.add(OrderId{101}, resting(Side::Sell, 101.0, 10.0));

    const auto result = match_against_book(book, OrderId{1}, Side::Buy, 5.0, buy_accepts(101.0));

    REQUIRE(result.trades.size() == 2);
    CHECK(result.trades[0].price == 100.0); // cheapest level consumed first
    CHECK(result.trades[0].quantity == 3.0);
    CHECK(result.trades[1].price == 101.0);
    CHECK(result.trades[1].quantity == 2.0); // 5 - 3
    CHECK(result.remaining == 0.0);
    CHECK_FALSE(book.empty(Side::Sell));
    CHECK(book.depth_at(Side::Sell, 101.0) == 8.0); // 10 - 2
}

TEST_CASE("match_against_book preserves FIFO time priority within one price level",
          "[core][match_against_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{100}, resting(Side::Sell, 100.0, 3.0));
    book.add(OrderId{101}, resting(Side::Sell, 100.0, 3.0));

    const auto result = match_against_book(book, OrderId{1}, Side::Buy, 4.0, buy_accepts(100.0));

    REQUIRE(result.trades.size() == 2);
    CHECK(result.trades[0].sell_order_id == OrderId{100}); // first in, first matched
    CHECK(result.trades[0].quantity == 3.0);
    CHECK(result.trades[1].sell_order_id == OrderId{101});
    CHECK(result.trades[1].quantity == 1.0);
}

TEST_CASE("match_against_book stops as soon as the best price no longer crosses",
          "[core][match_against_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{100}, resting(Side::Sell, 100.0, 3.0));
    book.add(OrderId{101}, resting(Side::Sell, 200.0, 10.0)); // too expensive for the aggressor

    const auto result = match_against_book(book, OrderId{1}, Side::Buy, 5.0, buy_accepts(100.0));

    REQUIRE(result.trades.size() == 1);
    CHECK(result.trades[0].price == 100.0);
    CHECK(result.remaining == 2.0);                  // stopped, 200.0 doesn't cross
    CHECK(book.depth_at(Side::Sell, 200.0) == 10.0); // untouched
}

TEST_CASE("match_against_book: a Sell aggressor matches against the Buy side with buy/sell "
          "ids assigned correctly",
          "[core][match_against_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{100}, resting(Side::Buy, 100.0, 5.0));

    const auto accepts_sell = [](double resting_price) {
        return crosses(Side::Sell, 100.0, resting_price);
    };
    const auto result = match_against_book(book, OrderId{1}, Side::Sell, 5.0, accepts_sell);

    REQUIRE(result.trades.size() == 1);
    CHECK(result.trades[0].buy_order_id == OrderId{100}); // the resting order
    CHECK(result.trades[0].sell_order_id == OrderId{1});  // the aggressor
}
