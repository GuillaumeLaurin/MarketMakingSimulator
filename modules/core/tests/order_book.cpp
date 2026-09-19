/// \file
/// Tests for OrderBook: the Side-routing and "best means highest for Buy,
/// lowest for Sell" logic it adds on top of PriceLevels. The underlying
/// per-level mechanics (FIFO, partial fills, level removal) are already
/// covered by price_levels.cpp and not re-tested here.

#include <catch2/catch_test_macros.hpp>

#include <core/order.hpp>
#include <core/order_book.hpp>
#include <core/order_id.hpp>
#include <core/side.hpp>

using mms::core::LimitOrder;
using mms::core::OrderBook;
using mms::core::OrderId;
using mms::core::Side;
using mms::core::TimeInForce;

namespace {

LimitOrder<double, double> make_order(Side side, double price, double quantity) {
    return LimitOrder<double, double>{side, price, quantity, TimeInForce::GTC};
}

} // namespace

TEST_CASE("OrderBook starts empty on both sides", "[core][order_book]") {
    const OrderBook<double, double> book;

    CHECK(book.empty(Side::Buy));
    CHECK(book.empty(Side::Sell));
    CHECK(book.size(Side::Buy) == 0);
    CHECK(book.size(Side::Sell) == 0);
    CHECK_FALSE(book.best(Side::Buy).has_value());
    CHECK_FALSE(book.best_price(Side::Sell).has_value());
}

TEST_CASE("OrderBook::best on the Buy side is the HIGHEST resting price", "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Buy, 99.0, 5.0));
    book.add(OrderId{2}, make_order(Side::Buy, 101.0, 5.0));
    book.add(OrderId{3}, make_order(Side::Buy, 100.0, 5.0));

    CHECK(book.best_price(Side::Buy) == 101.0);
    REQUIRE(book.best(Side::Buy).has_value());
    CHECK(book.best(Side::Buy)->id == OrderId{2});
}

TEST_CASE("OrderBook::best on the Sell side is the LOWEST resting price", "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Sell, 101.0, 5.0));
    book.add(OrderId{2}, make_order(Side::Sell, 99.0, 5.0));
    book.add(OrderId{3}, make_order(Side::Sell, 100.0, 5.0));

    CHECK(book.best_price(Side::Sell) == 99.0);
    REQUIRE(book.best(Side::Sell).has_value());
    CHECK(book.best(Side::Sell)->id == OrderId{2});
}

TEST_CASE("OrderBook keeps Buy and Sell completely independent", "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Buy, 100.0, 5.0));

    CHECK_FALSE(book.empty(Side::Buy));
    CHECK(book.empty(Side::Sell)); // untouched by the Buy-side add
    CHECK(book.size(Side::Buy) == 1);
    CHECK(book.size(Side::Sell) == 0);
    CHECK(book.depth_at(Side::Sell, 100.0) == 0.0);
}

TEST_CASE("OrderBook::cancel removes an entry from whichever side it rests on",
          "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Buy, 100.0, 5.0));
    book.add(OrderId{2}, make_order(Side::Sell, 101.0, 5.0));

    const bool cancelled = book.cancel(OrderId{1});

    CHECK(cancelled);
    CHECK(book.empty(Side::Buy));
    CHECK_FALSE(book.empty(Side::Sell)); // the other side is untouched
}

TEST_CASE("OrderBook::cancel of an unknown id is a no-op that reports failure",
          "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Buy, 100.0, 5.0));

    CHECK_FALSE(book.cancel(OrderId{999}));
    CHECK(book.size(Side::Buy) == 1); // untouched
}

TEST_CASE("OrderBook::fill routes to the correct side and updates depth", "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Sell, 100.0, 10.0));

    book.fill(OrderId{1}, 4.0);
    CHECK(book.depth_at(Side::Sell, 100.0) == 6.0);
    CHECK_FALSE(book.empty(Side::Sell));

    book.fill(OrderId{1}, 6.0);
    CHECK(book.empty(Side::Sell));
    CHECK(book.size(Side::Sell) == 0);
}

TEST_CASE("OrderBook::depth_at sums quantity at a price on the requested side only",
          "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Buy, 100.0, 3.0));
    book.add(OrderId{2}, make_order(Side::Buy, 100.0, 4.0));
    book.add(OrderId{3}, make_order(Side::Sell, 100.0, 50.0)); // same price, other side

    CHECK(book.depth_at(Side::Buy, 100.0) == 7.0);
    CHECK(book.depth_at(Side::Sell, 100.0) == 50.0);
}
