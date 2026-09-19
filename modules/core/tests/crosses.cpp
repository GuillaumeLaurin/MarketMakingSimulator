/// \file
/// Tests for crosses(): the price-crossing predicate, in complete
/// isolation from OrderBook/MatchingEngine.

#include <catch2/catch_test_macros.hpp>

#include <core/matching_engine.hpp>
#include <core/side.hpp>

using mms::core::crosses;
using mms::core::Side;

TEST_CASE("a Buy crosses a resting Sell at or below its price", "[core][crosses]") {
    CHECK(crosses(Side::Buy, 100.0, 99.0));         // resting cheaper than the buy limit: crosses
    CHECK(crosses(Side::Buy, 100.0, 100.0));        // exactly at the limit: crosses
    CHECK_FALSE(crosses(Side::Buy, 100.0, 100.01)); // resting above the limit: does not cross
}

TEST_CASE("a Sell crosses a resting Buy at or above its price", "[core][crosses]") {
    CHECK(crosses(Side::Sell, 100.0, 101.0));       // resting higher than the sell limit: crosses
    CHECK(crosses(Side::Sell, 100.0, 100.0));       // exactly at the limit: crosses
    CHECK_FALSE(crosses(Side::Sell, 100.0, 99.99)); // resting below the limit: does not cross
}
