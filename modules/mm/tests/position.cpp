/// \file
/// Tests for mm/position.hpp: average-cost accounting of one asset. Opening,
/// adding, partially and fully reducing, and reversing a position, on both
/// the long and the short side, then the accounting identity that pins the
/// whole thing down: realized + unrealized P&L equals cash plus the market
/// value of the position, over a long deterministic sequence of fills.

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/position.hpp>
#include <mm/types.hpp>

namespace mm = mms::mm;

namespace {

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

} // namespace

TEST_CASE("A new Position is flat", "[mm][position]") {
    const mm::Position position;

    CHECK(position.quantity() == 0);
    CHECK(position.average_cost() == 0.0);
    CHECK(position.realized_pnl() == 0.0);
    CHECK(position.unrealized_pnl(123.0) == 0.0);
}

TEST_CASE("Buying from flat opens a long at the fill price", "[mm][position]") {
    mm::Position position;

    const double realized = position.on_fill(mm::Side::Buy, 100.0, 10);

    CHECK(realized == 0.0);
    CHECK(position.quantity() == 10);
    CHECK(position.average_cost() == 100.0);
    CHECK(position.realized_pnl() == 0.0);
}

TEST_CASE("Selling from flat opens a short at the fill price", "[mm][position]") {
    mm::Position position;

    const double realized = position.on_fill(mm::Side::Sell, 100.0, 5);

    CHECK(realized == 0.0);
    CHECK(position.quantity() == -5);
    CHECK(position.average_cost() == 100.0);
}

TEST_CASE("Opening from flat takes the fill price exactly, with no arithmetic on it",
          "[mm][position]") {
    mm::Position position;
    const double awkward = 0.1 + 0.2; // 0.30000000000000004

    static_cast<void>(position.on_fill(mm::Side::Buy, awkward, 3));

    CHECK(position.average_cost() == awkward);
}

TEST_CASE("Adding to a long moves the average cost to the volume-weighted mean", "[mm][position]") {
    mm::Position position;
    static_cast<void>(position.on_fill(mm::Side::Buy, 100.0, 10));

    const double realized = position.on_fill(mm::Side::Buy, 110.0, 10);

    CHECK(realized == 0.0);
    CHECK(position.quantity() == 20);
    CHECK(position.average_cost() == 105.0);

    // Unequal sizes weigh unequally: (10 * 100 + 30 * 104) / 40 = 103.
    mm::Position weighted;
    static_cast<void>(weighted.on_fill(mm::Side::Buy, 100.0, 10));
    static_cast<void>(weighted.on_fill(mm::Side::Buy, 104.0, 30));
    CHECK(weighted.average_cost() == 103.0);
}

TEST_CASE("Adding to a short moves the average cost to the volume-weighted mean",
          "[mm][position]") {
    mm::Position position;
    static_cast<void>(position.on_fill(mm::Side::Sell, 100.0, 10));

    const double realized = position.on_fill(mm::Side::Sell, 110.0, 10);

    CHECK(realized == 0.0);
    CHECK(position.quantity() == -20);
    CHECK(position.average_cost() == 105.0);
}

TEST_CASE("Partially reducing a long realizes profit and keeps the average cost",
          "[mm][position]") {
    mm::Position position;
    static_cast<void>(position.on_fill(mm::Side::Buy, 100.0, 10));

    const double realized = position.on_fill(mm::Side::Sell, 110.0, 4);

    CHECK(realized == 40.0); // 4 * (110 - 100)
    CHECK(position.quantity() == 6);
    CHECK(position.average_cost() == 100.0); // the shares left were still bought at 100
    CHECK(position.realized_pnl() == 40.0);
    CHECK(position.unrealized_pnl(105.0) == 30.0); // 6 * (105 - 100)
}

TEST_CASE("Partially reducing a short realizes profit and keeps the average cost",
          "[mm][position]") {
    mm::Position position;
    static_cast<void>(position.on_fill(mm::Side::Sell, 100.0, 10));

    const double realized = position.on_fill(mm::Side::Buy, 90.0, 4);

    CHECK(realized == 40.0); // 4 * (100 - 90): a short gains when the price falls
    CHECK(position.quantity() == -6);
    CHECK(position.average_cost() == 100.0);
    CHECK(position.unrealized_pnl(95.0) == 30.0); // -6 * (95 - 100)
}

TEST_CASE("Reducing at a worse price realizes a loss", "[mm][position]") {
    mm::Position long_position;
    static_cast<void>(long_position.on_fill(mm::Side::Buy, 100.0, 10));
    CHECK(long_position.on_fill(mm::Side::Sell, 90.0, 5) == -50.0);

    mm::Position short_position;
    static_cast<void>(short_position.on_fill(mm::Side::Sell, 100.0, 10));
    CHECK(short_position.on_fill(mm::Side::Buy, 110.0, 5) == -50.0);
}

TEST_CASE("Closing a long exactly returns to flat with a zero average cost", "[mm][position]") {
    mm::Position position;
    static_cast<void>(position.on_fill(mm::Side::Buy, 100.0, 10));

    const double realized = position.on_fill(mm::Side::Sell, 103.0, 10);

    CHECK(realized == 30.0);
    CHECK(position.quantity() == 0);
    CHECK(position.average_cost() == 0.0);
    CHECK(position.realized_pnl() == 30.0);
    CHECK(position.unrealized_pnl(150.0) == 0.0);
}

TEST_CASE("Closing a short exactly returns to flat with a zero average cost", "[mm][position]") {
    mm::Position position;
    static_cast<void>(position.on_fill(mm::Side::Sell, 100.0, 10));

    const double realized = position.on_fill(mm::Side::Buy, 97.0, 10);

    CHECK(realized == 30.0);
    CHECK(position.quantity() == 0);
    CHECK(position.average_cost() == 0.0);
}

TEST_CASE("A sell larger than a long closes it and opens a short at the fill price",
          "[mm][position]") {
    mm::Position position;
    static_cast<void>(position.on_fill(mm::Side::Buy, 100.0, 5));

    const double realized = position.on_fill(mm::Side::Sell, 110.0, 8);

    CHECK(realized == 50.0); // only the 5 closed shares realize: 5 * (110 - 100)
    CHECK(position.quantity() == -3);
    CHECK(position.average_cost() == 110.0);       // the new short opened at the fill price
    CHECK(position.unrealized_pnl(105.0) == 15.0); // -3 * (105 - 110)
}

TEST_CASE("A buy larger than a short closes it and opens a long at the fill price",
          "[mm][position]") {
    mm::Position position;
    static_cast<void>(position.on_fill(mm::Side::Sell, 100.0, 5));

    const double realized = position.on_fill(mm::Side::Buy, 90.0, 8);

    CHECK(realized == 50.0); // 5 * (100 - 90)
    CHECK(position.quantity() == 3);
    CHECK(position.average_cost() == 90.0);
    CHECK(position.unrealized_pnl(95.0) == 15.0); // 3 * (95 - 90)
}

TEST_CASE("realized_pnl accumulates what every on_fill returned", "[mm][position]") {
    mm::Position position;
    double returned = 0.0;

    returned += position.on_fill(mm::Side::Buy, 100.0, 10);
    returned += position.on_fill(mm::Side::Sell, 104.0, 4);
    returned += position.on_fill(mm::Side::Sell, 98.0, 6);
    returned += position.on_fill(mm::Side::Sell, 100.0, 2);
    returned += position.on_fill(mm::Side::Buy, 95.0, 5);

    CHECK(position.realized_pnl() == returned);
    // +16 (4 of the long), -12 (the last 6), 0 (opens a 2-share short), +10 (5 buys: 2 close it).
    CHECK(returned == 14.0);
}

TEST_CASE("After going flat, a new position carries no memory of the old cost", "[mm][position]") {
    mm::Position position;
    static_cast<void>(position.on_fill(mm::Side::Buy, 100.0, 10));
    static_cast<void>(position.on_fill(mm::Side::Sell, 110.0, 10));

    static_cast<void>(position.on_fill(mm::Side::Buy, 120.0, 5));

    CHECK(position.quantity() == 5);
    CHECK(position.average_cost() == 120.0);
    CHECK(position.realized_pnl() == 100.0); // the earlier round trip is kept
}

TEST_CASE("unrealized_pnl is signed by direction and by the mark", "[mm][position]") {
    mm::Position long_position;
    static_cast<void>(long_position.on_fill(mm::Side::Buy, 100.0, 10));
    CHECK(long_position.unrealized_pnl(103.0) == 30.0);
    CHECK(long_position.unrealized_pnl(96.0) == -40.0);

    mm::Position short_position;
    static_cast<void>(short_position.on_fill(mm::Side::Sell, 100.0, 10));
    CHECK(short_position.unrealized_pnl(96.0) == 40.0);
    CHECK(short_position.unrealized_pnl(103.0) == -30.0);
}

TEST_CASE("realized + unrealized always equals cash plus the market value of the position",
          "[mm][position]") {
    // With no fees and a flat start, total P&L is cash + quantity * mark no
    // matter how the fills opened, added, reduced or reversed the position.
    // Prices sit on a 0.25 grid so every sum is exact in binary.
    constexpr double mark = 101.5;
    mm::Position position;
    double cash = 0.0;
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    std::vector<int> violations;

    for (int i = 0; i < 500; ++i) {
        state = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
        const mm::Side side = ((state >> 33U) & 1U) != 0U ? mm::Side::Buy : mm::Side::Sell;
        const auto quantity = static_cast<mm::Quantity>(1U + ((state >> 40U) % 9U));
        const double price = 100.0 + (static_cast<double>((state >> 20U) % 40U) * 0.25);

        static_cast<void>(position.on_fill(side, price, quantity));
        cash += (side == mm::Side::Buy ? -1.0 : 1.0) * price * mm::to_double(quantity);

        const double total = position.realized_pnl() + position.unrealized_pnl(mark);
        const double equity = cash + (mm::to_double(position.quantity()) * mark);
        if (total != Catch::Approx(equity).margin(1e-6)) {
            violations.push_back(i);
        }
    }
    CHECK(violations.empty());
}

TEST_CASE("on_fill rejects a non-positive quantity", "[mm][position]") {
    mm::Position position;

    CHECK_THROWS_AS(position.on_fill(mm::Side::Buy, 100.0, 0), std::invalid_argument);
    CHECK_THROWS_AS(position.on_fill(mm::Side::Sell, 100.0, -3), std::invalid_argument);
}

TEST_CASE("on_fill rejects a non-finite price", "[mm][position]") {
    mm::Position position;

    CHECK_THROWS_AS(position.on_fill(mm::Side::Buy, quiet_nan, 1), std::invalid_argument);
    CHECK_THROWS_AS(position.on_fill(mm::Side::Sell, infinity, 1), std::invalid_argument);
    CHECK_THROWS_AS(position.on_fill(mm::Side::Sell, -infinity, 1), std::invalid_argument);
}

TEST_CASE("A rejected fill leaves the position untouched", "[mm][position]") {
    mm::Position position;
    static_cast<void>(position.on_fill(mm::Side::Buy, 100.0, 10));

    CHECK_THROWS_AS(position.on_fill(mm::Side::Sell, quiet_nan, 4), std::invalid_argument);
    CHECK_THROWS_AS(position.on_fill(mm::Side::Sell, 110.0, 0), std::invalid_argument);

    CHECK(position.quantity() == 10);
    CHECK(position.average_cost() == 100.0);
    CHECK(position.realized_pnl() == 0.0);
}
