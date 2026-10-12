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
    return LimitOrder<double, double>{
        .side = side, .price = price, .quantity = quantity, .time_in_force = TimeInForce::GTC};
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

TEST_CASE("OrderBook::total_matchable on the Buy side walks from the HIGHEST price outward "
          "(same direction as best())",
          "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Buy, 100.0, 3.0));
    book.add(OrderId{2}, make_order(Side::Buy, 101.0, 5.0));
    book.add(OrderId{3}, make_order(Side::Buy, 102.0, 7.0));

    // A Sell aggressor crossing at or above 101.0 - Buy's own "best" (the
    // highest price, 102.0) must be the one checked first, matching best().
    const auto total = book.total_matchable(Side::Buy, [](double price) { return price >= 101.0; });
    CHECK(total == 12.0); // 102.0's 7 + 101.0's 5, NOT 100.0's 3

    // This is the exact scenario the direction bug broke: if Buy had walked
    // ascending (lowest first) instead, it would have stopped immediately
    // at 100.0 (which fails >= 101.0) and returned 0.0 instead of 12.0.
    CHECK(book.total_matchable(Side::Buy, [](double price) { return price >= 101.0; }) > 0.0);
}

TEST_CASE("OrderBook::total_matchable on the Sell side walks from the LOWEST price outward "
          "(same direction as best())",
          "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Sell, 100.0, 3.0));
    book.add(OrderId{2}, make_order(Side::Sell, 101.0, 5.0));
    book.add(OrderId{3}, make_order(Side::Sell, 102.0, 7.0));

    // A Buy aggressor crossing at or below 101.0 - Sell's own "best" (the
    // lowest price, 100.0) must be the one checked first, matching best().
    const auto total =
        book.total_matchable(Side::Sell, [](double price) { return price <= 101.0; });
    CHECK(total == 8.0); // 100.0's 3 + 101.0's 5, NOT 102.0's 7
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

TEST_CASE("OrderBook::levels lists the best levels first, with the total at each price",
          "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Buy, 99.0, 5.0));
    book.add(OrderId{2}, make_order(Side::Buy, 101.0, 2.0));
    book.add(OrderId{3}, make_order(Side::Buy, 101.0, 3.0));
    book.add(OrderId{4}, make_order(Side::Buy, 100.0, 4.0));
    book.add(OrderId{5}, make_order(Side::Sell, 103.0, 1.0));
    book.add(OrderId{6}, make_order(Side::Sell, 102.0, 6.0));
    book.add(OrderId{7}, make_order(Side::Sell, 102.0, 1.0));

    const auto bids = book.levels(Side::Buy, 2);
    REQUIRE(bids.size() == 2);
    CHECK(bids[0].price == 101.0); // the highest bid first, both orders added up
    CHECK(bids[0].quantity == 5.0);
    CHECK(bids[1].price == 100.0);
    CHECK(bids[1].quantity == 4.0);

    const auto asks = book.levels(Side::Sell, 5);
    REQUIRE(asks.size() == 2);     // fewer than asked: the side has two
    CHECK(asks[0].price == 102.0); // the lowest ask first
    CHECK(asks[0].quantity == 7.0);
    CHECK(asks[1].price == 103.0);
    CHECK(asks[1].quantity == 1.0);
}

TEST_CASE("OrderBook::levels of an empty side, or of zero levels, is empty", "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Buy, 99.0, 5.0));

    CHECK(book.levels(Side::Sell, 3).empty());
    CHECK(book.levels(Side::Buy, 0).empty());
}

TEST_CASE("OrderBook::levels follows the book as orders fill and leave", "[core][order_book]") {
    OrderBook<double, double> book;
    book.add(OrderId{1}, make_order(Side::Buy, 100.0, 5.0));
    book.add(OrderId{2}, make_order(Side::Buy, 100.0, 3.0));
    book.add(OrderId{3}, make_order(Side::Buy, 99.0, 4.0));

    book.fill(OrderId{1}, 2.0);
    CHECK(book.levels(Side::Buy, 1)[0].quantity == 6.0);

    book.cancel(OrderId{1});
    book.cancel(OrderId{2});
    const auto bids = book.levels(Side::Buy, 2);
    REQUIRE(bids.size() == 1);
    CHECK(bids[0].price == 99.0);
}
