/// \file
/// Tests for PriceLevels, on its own terms - independently of OrderBook and
/// with no Side/Buy/Sell branching to reason about at all (PriceLevels is
/// blind to what "best" means; that's OrderBook's job).

#include <catch2/catch_test_macros.hpp>

#include <core/order.hpp>
#include <core/order_book.hpp>
#include <core/order_id.hpp>
#include <core/side.hpp>

using mms::core::LimitOrder;
using mms::core::OrderId;
using mms::core::PriceLevels;
using mms::core::Side;
using mms::core::TimeInForce;

namespace {

LimitOrder<double, double> make_order(double price, double quantity) {
    return LimitOrder<double, double>{
        .side = Side::Buy, .price = price, .quantity = quantity, .time_in_force = TimeInForce::GTC};
}

} // namespace

TEST_CASE("PriceLevels starts empty", "[core][price_levels]") {
    const PriceLevels<double, double> levels;

    CHECK(levels.empty());
    CHECK(levels.size() == 0);
    CHECK_FALSE(levels.best_lowest().has_value());
    CHECK_FALSE(levels.best_highest().has_value());
    CHECK_FALSE(levels.lowest_price().has_value());
    CHECK_FALSE(levels.highest_price().has_value());
    CHECK(levels.depth_at(100.0) == 0.0);
}

TEST_CASE("PriceLevels::add makes one entry visible from every angle", "[core][price_levels]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, make_order(100.0, 10.0));

    CHECK_FALSE(levels.empty());
    CHECK(levels.size() == 1);
    CHECK(levels.lowest_price() == 100.0);
    CHECK(levels.highest_price() == 100.0);
    REQUIRE(levels.best_lowest().has_value());
    CHECK(levels.best_lowest()->id == OrderId{1});
    REQUIRE(levels.best_highest().has_value());
    CHECK(levels.best_highest()->id == OrderId{1});
    CHECK(levels.depth_at(100.0) == 10.0);
    CHECK(levels.depth_at(99.0) == 0.0);
}

TEST_CASE("PriceLevels orders distinct price levels correctly", "[core][price_levels]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, make_order(101.0, 5.0));
    levels.add(OrderId{2}, make_order(99.0, 5.0));
    levels.add(OrderId{3}, make_order(100.0, 5.0));

    CHECK(levels.size() == 3);
    CHECK(levels.lowest_price() == 99.0);
    CHECK(levels.highest_price() == 101.0);
    CHECK(levels.best_lowest()->id == OrderId{2});
    CHECK(levels.best_highest()->id == OrderId{1});
}

TEST_CASE("PriceLevels preserves FIFO time priority within one price level",
          "[core][price_levels]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, make_order(100.0, 5.0));
    levels.add(OrderId{2}, make_order(100.0, 7.0));
    levels.add(OrderId{3}, make_order(100.0, 3.0));

    // The first inserted at that price must come out first, regardless of
    // "lowest" or "highest" (a single-level query on either end sees the
    // same level, so the front of that level's queue).
    CHECK(levels.best_lowest()->id == OrderId{1});
    CHECK(levels.best_highest()->id == OrderId{1});
    CHECK(levels.depth_at(100.0) == 15.0); // 5 + 7 + 3
}

TEST_CASE("PriceLevels::remove deletes one entry, leaves the rest of the level intact",
          "[core][price_levels]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, make_order(100.0, 5.0));
    levels.add(OrderId{2}, make_order(100.0, 7.0));

    levels.remove(100.0, OrderId{1});

    CHECK(levels.size() == 1);
    CHECK(levels.depth_at(100.0) == 7.0);
    CHECK(levels.best_lowest()->id == OrderId{2});
}

TEST_CASE("PriceLevels::remove of the last entry at a price removes the whole level",
          "[core][price_levels]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, make_order(100.0, 5.0));
    levels.add(OrderId{2}, make_order(200.0, 5.0));

    levels.remove(100.0, OrderId{1});

    CHECK(levels.size() == 1);
    CHECK(levels.lowest_price() == 200.0); // the 100.0 level is gone entirely
    CHECK(levels.depth_at(100.0) == 0.0);
}

TEST_CASE("PriceLevels::reduce partially fills without removing the entry",
          "[core][price_levels]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, make_order(100.0, 10.0));

    const bool removed = levels.reduce(100.0, OrderId{1}, 4.0);

    CHECK_FALSE(removed);
    CHECK(levels.size() == 1);
    CHECK(levels.depth_at(100.0) == 6.0);
    CHECK(levels.best_lowest()->order.quantity == 6.0);
}

TEST_CASE("PriceLevels::reduce to exactly zero removes the entry", "[core][price_levels]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, make_order(100.0, 10.0));

    const bool removed = levels.reduce(100.0, OrderId{1}, 10.0);

    CHECK(removed);
    CHECK(levels.empty());
    CHECK(levels.size() == 0);
}

TEST_CASE("PriceLevels::total_matchable_ascending sums from the lowest price outward, "
          "stopping at the first rejected price",
          "[core][price_levels]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, make_order(100.0, 3.0));
    levels.add(OrderId{2}, make_order(101.0, 5.0));
    levels.add(OrderId{3}, make_order(102.0, 7.0));

    // accepts everything <= 101.0: sums the 100.0 and 101.0 levels, stops
    // before reaching 102.0.
    const auto total =
        levels.total_matchable_ascending([](double price) { return price <= 101.0; });
    CHECK(total == 8.0); // 3 + 5, NOT + 7

    // accepts nothing: the very first (lowest) level already fails.
    CHECK(levels.total_matchable_ascending([](double) { return false; }) == 0.0);

    // accepts everything: sums the whole book.
    CHECK(levels.total_matchable_ascending([](double) { return true; }) == 15.0); // 3 + 5 + 7
}

TEST_CASE("PriceLevels::total_matchable_descending sums from the highest price outward, "
          "stopping at the first rejected price",
          "[core][price_levels]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, make_order(100.0, 3.0));
    levels.add(OrderId{2}, make_order(101.0, 5.0));
    levels.add(OrderId{3}, make_order(102.0, 7.0));

    // accepts everything >= 101.0: sums the 102.0 and 101.0 levels (walked
    // highest-first), stops before reaching 100.0.
    const auto total =
        levels.total_matchable_descending([](double price) { return price >= 101.0; });
    CHECK(total == 12.0); // 7 + 5, NOT + 3

    CHECK(levels.total_matchable_descending([](double) { return false; }) == 0.0);
    CHECK(levels.total_matchable_descending([](double) { return true; }) == 15.0);
}

TEST_CASE("PriceLevels::total_matchable on an empty book is zero regardless of the predicate",
          "[core][price_levels]") {
    const PriceLevels<double, double> levels;

    CHECK(levels.total_matchable_ascending([](double) { return true; }) == 0.0);
    CHECK(levels.total_matchable_descending([](double) { return true; }) == 0.0);
}

TEST_CASE("PriceLevels::reduce preserves FIFO order of what remains", "[core][price_levels]") {
    PriceLevels<double, double> levels;
    levels.add(OrderId{1}, make_order(100.0, 5.0));
    levels.add(OrderId{2}, make_order(100.0, 5.0));

    // First entry partially filled, still resting, still first in line.
    levels.reduce(100.0, OrderId{1}, 2.0);
    CHECK(levels.best_lowest()->id == OrderId{1});
    CHECK(levels.best_lowest()->order.quantity == 3.0);

    // First entry fully filled now: second entry becomes best.
    levels.reduce(100.0, OrderId{1}, 3.0);
    CHECK(levels.best_lowest()->id == OrderId{2});
}
