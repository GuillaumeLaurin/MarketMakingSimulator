/// \file
/// Tests for agents/sizing.hpp: how much an agent may trade without going past a position limit
/// of its own. A trade the other way always has room, and a position that is already past the
/// limit leaves no room on the side that makes it worse.

#include <agents/sizing.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

namespace agents = mms::agents;
using mms::mm::Side;

} // namespace

// The function is constexpr, so a few of its answers are checked at compile time.
static_assert(agents::clip_to_limit(Side::Buy, 0, 3, 10) == 3);
static_assert(agents::clip_to_limit(Side::Sell, 0, 3, 10) == 3);
static_assert(agents::clip_to_limit(Side::Buy, 8, 3, 10) == 2);

TEST_CASE("With room to spare the whole size goes", "[agents][sizing]") {
    CHECK(agents::clip_to_limit(Side::Buy, 0, 3, 10) == 3);
    CHECK(agents::clip_to_limit(Side::Sell, 0, 3, 10) == 3);
    CHECK(agents::clip_to_limit(Side::Buy, -5, 3, 10) == 3);
    CHECK(agents::clip_to_limit(Side::Sell, 5, 3, 10) == 3);
}

TEST_CASE("A long position leaves less room to buy, not to sell", "[agents][sizing]") {
    CHECK(agents::clip_to_limit(Side::Buy, 8, 3, 10) == 2);
    CHECK(agents::clip_to_limit(Side::Buy, 9, 3, 10) == 1);
    CHECK(agents::clip_to_limit(Side::Sell, 8, 3, 10) == 3);
}

TEST_CASE("A short position leaves less room to sell, not to buy", "[agents][sizing]") {
    CHECK(agents::clip_to_limit(Side::Sell, -8, 3, 10) == 2);
    CHECK(agents::clip_to_limit(Side::Sell, -9, 3, 10) == 1);
    CHECK(agents::clip_to_limit(Side::Buy, -8, 3, 10) == 3);
}

TEST_CASE("At the limit there is no room on that side", "[agents][sizing]") {
    CHECK(agents::clip_to_limit(Side::Buy, 10, 3, 10) == 0);
    CHECK(agents::clip_to_limit(Side::Sell, -10, 3, 10) == 0);
    CHECK(agents::clip_to_limit(Side::Sell, 10, 3, 10) == 3);
    CHECK(agents::clip_to_limit(Side::Buy, -10, 3, 10) == 3);
}

TEST_CASE("Past the limit there is no room to make it worse, but there is to come back",
          "[agents][sizing]") {
    CHECK(agents::clip_to_limit(Side::Buy, 14, 3, 10) == 0);
    CHECK(agents::clip_to_limit(Side::Sell, -14, 3, 10) == 0);
    CHECK(agents::clip_to_limit(Side::Sell, 14, 3, 10) == 3);
    CHECK(agents::clip_to_limit(Side::Buy, -14, 3, 10) == 3);
}

TEST_CASE("The size is never exceeded, however much room there is", "[agents][sizing]") {
    CHECK(agents::clip_to_limit(Side::Buy, 0, 1, 1000) == 1);
    CHECK(agents::clip_to_limit(Side::Sell, 0, 7, 1000) == 7);
}

TEST_CASE("A size of zero is a trade of zero", "[agents][sizing]") {
    CHECK(agents::clip_to_limit(Side::Buy, 0, 0, 10) == 0);
    CHECK(agents::clip_to_limit(Side::Sell, 0, 0, 10) == 0);
}
