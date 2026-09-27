/// \file
/// Tests for mm/tick.hpp. The interesting part is floating-point: a naive
/// floor(price / tick) puts 100.3 on tick 1002 instead of 1003 and a naive
/// ceil(price / tick) puts 1.11 on tick 112 instead of 111, so those exact
/// traps are pinned here (and shown to be real before being relied on),
/// together with the outward rounding of a bid and an ask, the round trip
/// through price_at, quote_ticks' at-least-one-tick guarantee, and validation.

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/tick.hpp>
#include <mm/types.hpp>

namespace mm = mms::mm;

namespace {

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

} // namespace

TEST_CASE("bid_ticks rounds down and ask_ticks rounds up", "[mm][tick]") {
    // 100.2 sits at 200.4 ticks of 0.5.
    CHECK(mm::bid_ticks(100.2, 0.5) == 200);
    CHECK(mm::ask_ticks(100.2, 0.5) == 201);
}

TEST_CASE("A price exactly on the grid is its own tick for bid and ask alike", "[mm][tick]") {
    CHECK(mm::bid_ticks(100.0, 0.5) == 200);
    CHECK(mm::ask_ticks(100.0, 0.5) == 200);
    CHECK(mm::nearest_ticks(100.0, 0.5) == 200);
}

TEST_CASE("Negative and zero prices round outward too", "[mm][tick]") {
    CHECK(mm::bid_ticks(-0.25, 0.5) == -1);
    CHECK(mm::ask_ticks(-0.25, 0.5) == 0);
    CHECK(mm::bid_ticks(0.0, 0.5) == 0);
    CHECK(mm::ask_ticks(0.0, 0.5) == 0);
}

TEST_CASE("A naive floor(price / tick) is wrong on the grid, bid_ticks is not", "[mm][tick]") {
    // The traps are real: dividing lands just BELOW the whole number.
    REQUIRE(std::floor(100.3 / 0.1) == 1002.0);
    REQUIRE(std::floor(0.3 / 0.1) == 2.0);

    CHECK(mm::bid_ticks(100.3, 0.1) == 1003);
    CHECK(mm::bid_ticks(0.3, 0.1) == 3);
}

TEST_CASE("A naive ceil(price / tick) is wrong on the grid, ask_ticks is not", "[mm][tick]") {
    // Here the division lands just ABOVE the whole number.
    REQUIRE(std::ceil(1.11 / 0.01) == 112.0);
    REQUIRE(std::ceil(0.07 / 0.01) == 8.0);

    CHECK(mm::ask_ticks(1.11, 0.01) == 111);
    CHECK(mm::ask_ticks(0.07, 0.01) == 7);
}

TEST_CASE("Snapping does not swallow a genuinely off-grid price", "[mm][tick]") {
    // 1e-6 above and below the 100.3 grid line (0.00001 of a tick) is real.
    CHECK(mm::bid_ticks(100.3 + 1e-6, 0.1) == 1003);
    CHECK(mm::ask_ticks(100.3 + 1e-6, 0.1) == 1004);

    CHECK(mm::bid_ticks(100.3 - 1e-6, 0.1) == 1002);
    CHECK(mm::ask_ticks(100.3 - 1e-6, 0.1) == 1003);
}

TEST_CASE("bid_ticks never exceeds ask_ticks, and they differ exactly off the grid", "[mm][tick]") {
    std::vector<double> disagreements;
    for (int i = 0; i < 2000; ++i) {
        const double price = 50.0 + (i * 0.0173); // mostly off the 0.05 grid
        const mm::Ticks bid = mm::bid_ticks(price, 0.05);
        const mm::Ticks ask = mm::ask_ticks(price, 0.05);
        if (bid > ask || ask - bid > 1) {
            disagreements.push_back(price);
        }
    }
    CHECK(disagreements.empty());
}

TEST_CASE("nearest_ticks picks the closest line", "[mm][tick]") {
    CHECK(mm::nearest_ticks(100.24, 0.5) == 200);
    CHECK(mm::nearest_ticks(100.26, 0.5) == 201);
    CHECK(mm::nearest_ticks(-100.26, 0.5) == -201);
}

TEST_CASE("price_at converts a tick count to a price", "[mm][tick]") {
    CHECK(mm::price_at(0, 0.5) == 0.0);
    CHECK(mm::price_at(200, 0.5) == 100.0);
    CHECK(mm::price_at(-3, 0.25) == -0.75);
    CHECK(mm::price_at(1003, 0.1) == Catch::Approx(100.3).margin(1e-12));
}

TEST_CASE("price_at then bid_ticks, ask_ticks and nearest_ticks returns the same tick",
          "[mm][tick]") {
    // price_at(n) is n * tick, which is off the true grid line by an ulp or
    // two; every conversion back must still land on n.
    std::vector<mm::Ticks> wrong;
    for (const double tick : {0.01, 0.05, 0.1, 0.25, 0.5, 1.0}) {
        for (mm::Ticks n = -5000; n <= 50000; ++n) {
            const mm::Price price = mm::price_at(n, tick);
            if (mm::bid_ticks(price, tick) != n || mm::ask_ticks(price, tick) != n ||
                mm::nearest_ticks(price, tick) != n) {
                wrong.push_back(n);
            }
        }
    }
    CHECK(wrong.empty());
}

TEST_CASE("Every conversion rejects an invalid tick size", "[mm][tick]") {
    for (const double bad : {0.0, -0.01, quiet_nan, infinity, -infinity}) {
        CHECK_THROWS_AS(mm::bid_ticks(100.0, bad), std::invalid_argument);
        CHECK_THROWS_AS(mm::ask_ticks(100.0, bad), std::invalid_argument);
        CHECK_THROWS_AS(mm::nearest_ticks(100.0, bad), std::invalid_argument);
        CHECK_THROWS_AS(mm::price_at(10, bad), std::invalid_argument);
    }
}

TEST_CASE("Conversions from a price reject a non-finite or out-of-range price", "[mm][tick]") {
    for (const double bad : {quiet_nan, infinity, -infinity}) {
        CHECK_THROWS_AS(mm::bid_ticks(bad, 0.05), std::invalid_argument);
        CHECK_THROWS_AS(mm::ask_ticks(bad, 0.05), std::invalid_argument);
        CHECK_THROWS_AS(mm::nearest_ticks(bad, 0.05), std::invalid_argument);
    }

    // Finite, but far more ticks from zero than an int64 can count.
    CHECK_THROWS_AS(mm::bid_ticks(1.0e19, 1.0), std::invalid_argument);
    CHECK_THROWS_AS(mm::ask_ticks(1.0e300, 0.01), std::invalid_argument);
    CHECK_THROWS_AS(mm::nearest_ticks(-1.0e19, 1.0), std::invalid_argument);
}

TEST_CASE("quote_ticks rounds the bid down and the ask up", "[mm][tick]") {
    // 99.87 is 1997.4 ticks of 0.05, 100.13 is 2002.6 ticks.
    const mm::BidAskTicks ticks = mm::quote_ticks(99.87, 100.13, 0.05);

    CHECK(ticks.bid == 1997);
    CHECK(ticks.ask == 2003);
    CHECK(mm::price_at(ticks.bid, 0.05) == Catch::Approx(99.85).margin(1e-12));
    CHECK(mm::price_at(ticks.ask, 0.05) == Catch::Approx(100.15).margin(1e-12));
}

TEST_CASE("quote_ticks leaves on-grid prices where they are", "[mm][tick]") {
    const mm::BidAskTicks ticks = mm::quote_ticks(99.5, 100.5, 0.5);

    CHECK(ticks.bid == 199);
    CHECK(ticks.ask == 201);
}

TEST_CASE("quote_ticks keeps a thin positive spread at least one tick wide", "[mm][tick]") {
    // A one-cent raw spread on a half-dollar tick: outward rounding alone
    // separates them, with no repair needed.
    const mm::BidAskTicks ticks = mm::quote_ticks(100.01, 100.02, 0.5);

    CHECK(ticks.bid == 200);
    CHECK(ticks.ask == 201);
}

TEST_CASE("quote_ticks repairs a raw spread so thin both prices snap to one line", "[mm][tick]") {
    // Equal on-grid prices: both land on tick 200, the ask moves up one tick.
    const mm::BidAskTicks equal = mm::quote_ticks(100.0, 100.0, 0.5);
    CHECK(equal.bid == 200);
    CHECK(equal.ask == 201);

    // Strictly ordered, but 1e-13 apart around a grid line: snapping puts
    // both on tick 200, and the same repair applies.
    const mm::BidAskTicks thin = mm::quote_ticks(100.0 - 1e-13, 100.0 + 1e-13, 0.5);
    CHECK(thin.bid == 200);
    CHECK(thin.ask == 201);
}

TEST_CASE("quote_ticks on equal off-grid prices needs no repair", "[mm][tick]") {
    // Rounding outward already splits 100.2 into 200 (bid) and 201 (ask).
    const mm::BidAskTicks ticks = mm::quote_ticks(100.2, 100.2, 0.5);

    CHECK(ticks.bid == 200);
    CHECK(ticks.ask == 201);
}

TEST_CASE("quote_ticks always yields ask >= bid + 1 and only widens the raw quotes", "[mm][tick]") {
    constexpr double tick = 0.05;
    std::vector<int> violations;

    for (int i = 0; i < 1000; ++i) {
        const double raw_bid = 90.0 + (i * 0.0137);
        const double raw_ask = raw_bid + 0.001 + ((i % 7) * 0.0311);
        const mm::BidAskTicks ticks = mm::quote_ticks(raw_bid, raw_ask, tick);

        const bool separated = ticks.ask >= ticks.bid + 1;
        const bool bid_not_raised = mm::price_at(ticks.bid, tick) <= raw_bid + 1e-9;
        const bool ask_not_lowered = mm::price_at(ticks.ask, tick) >= raw_ask - 1e-9;
        if (!separated || !bid_not_raised || !ask_not_lowered) {
            violations.push_back(i);
        }
    }
    CHECK(violations.empty());
}

TEST_CASE("quote_ticks rejects a bid above the ask", "[mm][tick]") {
    CHECK_THROWS_AS(mm::quote_ticks(100.1, 100.0, 0.05), std::invalid_argument);
}

TEST_CASE("quote_ticks rejects non-finite prices and an invalid tick", "[mm][tick]") {
    CHECK_THROWS_AS(mm::quote_ticks(quiet_nan, 100.0, 0.05), std::invalid_argument);
    CHECK_THROWS_AS(mm::quote_ticks(99.0, quiet_nan, 0.05), std::invalid_argument);
    CHECK_THROWS_AS(mm::quote_ticks(99.0, infinity, 0.05), std::invalid_argument);
    CHECK_THROWS_AS(mm::quote_ticks(99.0, 101.0, 0.0), std::invalid_argument);
}
