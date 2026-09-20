/// \file
/// Tests for mm/pnl.hpp: PnlBreakdown arithmetic, and PnlLedger's inventory
/// P&L (the mid moving while a position is held), spread capture (how far
/// from the mid a fill happened), fees and rebates, and the two accounting
/// identities that make the decomposition trustworthy: realized + unrealized
/// equals spread capture + inventory, and both equal cash plus the market
/// value of the position.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/pnl.hpp>
#include <mm/types.hpp>

namespace mm = mms::mm;

namespace {

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

mm::PnlLedger ledger_of(std::initializer_list<mm::LedgerAsset> assets) {
    return mm::PnlLedger{std::vector<mm::LedgerAsset>{assets}};
}

void expect_zero(const mm::PnlBreakdown& breakdown) {
    CHECK(breakdown.realized == 0.0);
    CHECK(breakdown.unrealized == 0.0);
    CHECK(breakdown.spread_capture == 0.0);
    CHECK(breakdown.inventory == 0.0);
    CHECK(breakdown.fees == 0.0);
}

void expect_same(const mm::PnlBreakdown& actual, const mm::PnlBreakdown& expected) {
    CHECK(actual.realized == expected.realized);
    CHECK(actual.unrealized == expected.unrealized);
    CHECK(actual.spread_capture == expected.spread_capture);
    CHECK(actual.inventory == expected.inventory);
    CHECK(actual.fees == expected.fees);
}

} // namespace

TEST_CASE("A default PnlBreakdown is all zero", "[mm][pnl]") {
    const mm::PnlBreakdown breakdown;

    expect_zero(breakdown);
    CHECK(breakdown.gross() == 0.0);
    CHECK(breakdown.total() == 0.0);
}

TEST_CASE("PnlBreakdown::gross excludes fees and total includes them", "[mm][pnl]") {
    const mm::PnlBreakdown breakdown{.realized = 10.0,
                                     .unrealized = 5.0,
                                     .spread_capture = 12.0,
                                     .inventory = 3.0,
                                     .fees = -4.0};

    CHECK(breakdown.gross() == 15.0);
    CHECK(breakdown.total() == 11.0);
}

TEST_CASE("PnlBreakdown::operator+= adds every field", "[mm][pnl]") {
    mm::PnlBreakdown sum{
        .realized = 1.0, .unrealized = 2.0, .spread_capture = 3.0, .inventory = 4.0, .fees = 5.0};
    const mm::PnlBreakdown other{.realized = 10.0,
                                 .unrealized = 20.0,
                                 .spread_capture = 30.0,
                                 .inventory = 40.0,
                                 .fees = 50.0};

    sum += other;

    CHECK(sum.realized == 11.0);
    CHECK(sum.unrealized == 22.0);
    CHECK(sum.spread_capture == 33.0);
    CHECK(sum.inventory == 44.0);
    CHECK(sum.fees == 55.0);
}

TEST_CASE("A new ledger is flat, with every P&L at zero", "[mm][pnl]") {
    const mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}});

    REQUIRE(ledger.asset_count() == 2);
    CHECK(ledger.last_mid(0) == 100.0);
    CHECK(ledger.last_mid(1) == 50.0);
    CHECK(ledger.position(0).quantity() == 0);
    CHECK(ledger.position(1).quantity() == 0);
    expect_zero(ledger.breakdown(0));
    expect_zero(ledger.breakdown(1));
    expect_zero(ledger.portfolio());
}

TEST_CASE("The constructor rejects an empty list and non-finite inputs", "[mm][pnl]") {
    CHECK_THROWS_AS(mm::PnlLedger(std::vector<mm::LedgerAsset>{}), std::invalid_argument);
    CHECK_THROWS_AS(ledger_of({{.initial_mid = quiet_nan}}), std::invalid_argument);
    CHECK_THROWS_AS(ledger_of({{.initial_mid = infinity}}), std::invalid_argument);
    CHECK_THROWS_AS(ledger_of({{.initial_mid = 100.0, .maker_fee_bps = quiet_nan}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(ledger_of({{.initial_mid = 100.0, .maker_fee_bps = -infinity}}),
                    std::invalid_argument);
}

TEST_CASE("An unknown asset id throws std::out_of_range everywhere", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});

    CHECK_THROWS_AS(ledger.on_mark(1, 100.0), std::out_of_range);
    CHECK_THROWS_AS(ledger.on_fill(1, mm::Side::Buy, 100.0, 1, 100.0), std::out_of_range);
    CHECK_THROWS_AS(ledger.position(1), std::out_of_range);
    CHECK_THROWS_AS(ledger.last_mid(1), std::out_of_range);
    CHECK_THROWS_AS(ledger.breakdown(1), std::out_of_range);
}

TEST_CASE("Marking a flat asset only moves its last mid", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});

    ledger.on_mark(0, 110.0);

    CHECK(ledger.last_mid(0) == 110.0);
    expect_zero(ledger.breakdown(0));
}

TEST_CASE("A long position earns inventory P&L as the mid rises and loses it as it falls",
          "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    ledger.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0); // at the mid: no spread capture

    ledger.on_mark(0, 103.0);
    CHECK(ledger.breakdown(0).inventory == 30.0);

    ledger.on_mark(0, 101.0); // gives back 10 * 2
    CHECK(ledger.breakdown(0).inventory == 10.0);
    CHECK(ledger.breakdown(0).unrealized == 10.0);
    CHECK(ledger.breakdown(0).spread_capture == 0.0);
}

TEST_CASE("A short position earns inventory P&L as the mid falls", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    ledger.on_fill(0, mm::Side::Sell, 100.0, 10, 100.0);

    ledger.on_mark(0, 98.0);

    CHECK(ledger.breakdown(0).inventory == 20.0);
    CHECK(ledger.breakdown(0).unrealized == 20.0);
}

TEST_CASE("Spread capture is positive when buying under the mid or selling over it", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});

    ledger.on_fill(0, mm::Side::Buy, 99.0, 5, 100.0); // 5 shares, 1 under the mid
    CHECK(ledger.breakdown(0).spread_capture == 5.0);

    ledger.on_fill(0, mm::Side::Buy, 101.0, 2, 100.0); // paid 1 over the mid
    CHECK(ledger.breakdown(0).spread_capture == 3.0);

    ledger.on_fill(0, mm::Side::Sell, 101.0, 3, 100.0); // sold 1 over the mid
    CHECK(ledger.breakdown(0).spread_capture == 6.0);

    ledger.on_fill(0, mm::Side::Sell, 99.0, 4, 100.0); // sold 1 under the mid
    CHECK(ledger.breakdown(0).spread_capture == 2.0);
}

TEST_CASE("A fill exactly at the mid captures no spread", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});

    ledger.on_fill(0, mm::Side::Buy, 100.0, 7, 100.0);
    ledger.on_fill(0, mm::Side::Sell, 100.0, 3, 100.0);

    CHECK(ledger.breakdown(0).spread_capture == 0.0);
}

TEST_CASE("on_fill applies the mid first, so inventory P&L needs no separate on_mark",
          "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    ledger.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0);

    // The mid is now 103 and a sale of 4 goes through at 104.
    ledger.on_fill(0, mm::Side::Sell, 104.0, 4, 103.0);

    const mm::PnlBreakdown breakdown = ledger.breakdown(0);
    CHECK(ledger.last_mid(0) == 103.0);
    CHECK(breakdown.inventory == 30.0);     // the 10 held rose by 3, BEFORE the sale
    CHECK(breakdown.spread_capture == 4.0); // 4 * (104 - 103)
    CHECK(breakdown.realized == 16.0);      // 4 * (104 - 100)
    CHECK(breakdown.unrealized == 18.0);    // the 6 left, 3 above their cost
    CHECK(breakdown.gross() == 34.0);
    CHECK(breakdown.spread_capture + breakdown.inventory == 34.0);
}

TEST_CASE("Assets are independent of each other", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}});

    ledger.on_fill(0, mm::Side::Buy, 99.0, 10, 100.0);
    ledger.on_mark(0, 102.0);

    expect_zero(ledger.breakdown(1));
    CHECK(ledger.last_mid(1) == 50.0);
    CHECK(ledger.position(1).quantity() == 0);
}

TEST_CASE("portfolio sums the breakdown of every asset", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}});
    ledger.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0);
    ledger.on_mark(0, 102.0);
    ledger.on_fill(1, mm::Side::Sell, 50.0, 5, 50.0);
    ledger.on_mark(1, 49.0);

    const mm::PnlBreakdown portfolio = ledger.portfolio();
    const mm::PnlBreakdown first = ledger.breakdown(0);
    const mm::PnlBreakdown second = ledger.breakdown(1);

    CHECK(portfolio.unrealized == 25.0); // 20 + 5
    CHECK(portfolio.inventory == 25.0);
    CHECK(portfolio.unrealized == first.unrealized + second.unrealized);
    CHECK(portfolio.inventory == first.inventory + second.inventory);
    CHECK(portfolio.spread_capture == first.spread_capture + second.spread_capture);
    CHECK(portfolio.realized == first.realized + second.realized);
    CHECK(portfolio.fees == first.fees + second.fees);
}

TEST_CASE("quantities lists the signed inventory in AssetId order", "[mm][pnl]") {
    mm::PnlLedger ledger =
        ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}, {.initial_mid = 25.0}});
    ledger.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0);
    ledger.on_fill(2, mm::Side::Sell, 25.0, 4, 25.0);

    const std::vector<mm::Quantity> quantities = ledger.quantities();

    CHECK(quantities == std::vector<mm::Quantity>{10, 0, -4});
}

TEST_CASE("position exposes the asset's average-cost Position", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    ledger.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0);
    ledger.on_fill(0, mm::Side::Buy, 110.0, 10, 100.0);

    CHECK(ledger.position(0).quantity() == 20);
    CHECK(ledger.position(0).average_cost() == 105.0);
}

TEST_CASE("A maker fee is charged on the notional of every fill, on both sides", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0, .maker_fee_bps = 2.0}});

    ledger.on_fill(0, mm::Side::Buy, 100.0, 1000, 100.0); // notional 100000 at 2 bps: 20
    CHECK(ledger.breakdown(0).fees == Catch::Approx(-20.0).margin(1e-9));

    ledger.on_fill(0, mm::Side::Sell, 100.0, 500, 100.0); // notional 50000 at 2 bps: 10
    CHECK(ledger.breakdown(0).fees == Catch::Approx(-30.0).margin(1e-9));
}

TEST_CASE("A negative fee rate is a rebate that adds to the P&L", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 50.0, .maker_fee_bps = -1.0}});

    ledger.on_fill(0, mm::Side::Sell, 50.0, 200, 50.0); // notional 10000 at -1 bp: +1

    CHECK(ledger.breakdown(0).fees == Catch::Approx(1.0).margin(1e-9));
    CHECK(ledger.breakdown(0).total() == Catch::Approx(1.0).margin(1e-9));
}

TEST_CASE("Each asset uses its own fee rate", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0, .maker_fee_bps = 2.0},
                                      {.initial_mid = 100.0, .maker_fee_bps = 0.0}});

    ledger.on_fill(0, mm::Side::Buy, 100.0, 1000, 100.0);
    ledger.on_fill(1, mm::Side::Buy, 100.0, 1000, 100.0);

    CHECK(ledger.breakdown(0).fees == Catch::Approx(-20.0).margin(1e-9));
    CHECK(ledger.breakdown(1).fees == 0.0);
}

TEST_CASE("Fees touch only the fees bucket, and the total is gross plus fees", "[mm][pnl]") {
    mm::PnlLedger with_fee = ledger_of({{.initial_mid = 100.0, .maker_fee_bps = 5.0}});
    mm::PnlLedger without_fee = ledger_of({{.initial_mid = 100.0}});
    for (mm::PnlLedger* ledger : {&with_fee, &without_fee}) {
        ledger->on_fill(0, mm::Side::Buy, 99.0, 10, 100.0);
        ledger->on_mark(0, 102.0);
        ledger->on_fill(0, mm::Side::Sell, 103.0, 4, 102.0);
    }

    const mm::PnlBreakdown a = with_fee.breakdown(0);
    const mm::PnlBreakdown b = without_fee.breakdown(0);

    CHECK(a.realized == b.realized);
    CHECK(a.unrealized == b.unrealized);
    CHECK(a.spread_capture == b.spread_capture);
    CHECK(a.inventory == b.inventory);
    CHECK(a.fees < 0.0);
    CHECK(b.fees == 0.0);
    CHECK(a.total() == Catch::Approx(a.gross() + a.fees).margin(1e-12));
}

TEST_CASE("realized + unrealized equals spread capture + inventory, and both equal cash plus "
          "the position's market value",
          "[mm][pnl]") {
    // Three assets, marks and fills interleaved, on a 0.25 grid so every sum
    // is exact in binary. Fills happen 0 to 3 ticks either side of the mid,
    // in both directions, so positions open, add, reduce and reverse.
    mm::PnlLedger ledger =
        ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}, {.initial_mid = 25.0}});
    std::vector<double> mids{100.0, 50.0, 25.0};
    std::vector<double> cash(3, 0.0);
    std::uint64_t state = 0xD1B54A32D192ED03ULL;
    std::vector<int> two_views_disagree;
    std::vector<int> cash_disagrees;

    for (int step = 0; step < 600; ++step) {
        state = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
        const auto asset = static_cast<mm::AssetId>((state >> 33U) % 3U);
        mids[asset] += (static_cast<double>((state >> 20U) % 5U) - 2.0) * 0.25;

        if ((state >> 45U) % 3U == 0U) {
            const bool buys = ((state >> 12U) & 1U) != 0U;
            const auto quantity = static_cast<mm::Quantity>(1U + ((state >> 40U) % 9U));
            const double offset = static_cast<double>((state >> 8U) % 4U) * 0.25;
            const double price = mids[asset] + (buys ? -offset : offset);
            ledger.on_fill(
                asset, buys ? mm::Side::Buy : mm::Side::Sell, price, quantity, mids[asset]);
            cash[asset] += (buys ? -1.0 : 1.0) * price * mm::to_double(quantity);
        } else {
            ledger.on_mark(asset, mids[asset]);
        }

        for (mm::AssetId a = 0; a < 3; ++a) {
            const mm::PnlBreakdown b = ledger.breakdown(a);
            const double equity =
                cash[a] + (mm::to_double(ledger.position(a).quantity()) * ledger.last_mid(a));
            if (b.realized + b.unrealized !=
                Catch::Approx(b.spread_capture + b.inventory).margin(1e-6)) {
                two_views_disagree.push_back(step);
            }
            if (b.gross() != Catch::Approx(equity).margin(1e-6)) {
                cash_disagrees.push_back(step);
            }
        }
    }
    CHECK(two_views_disagree.empty());
    CHECK(cash_disagrees.empty());
}

TEST_CASE("With fees, total equals cash after fees plus the market value", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0, .maker_fee_bps = 3.0}});
    double cash = 0.0;

    ledger.on_fill(0, mm::Side::Buy, 99.5, 100, 100.0);
    cash -= 99.5 * 100.0;
    cash -= 99.5 * 100.0 * 3.0 * 1.0e-4;
    ledger.on_mark(0, 101.0);
    ledger.on_fill(0, mm::Side::Sell, 101.5, 60, 101.0);
    cash += 101.5 * 60.0;
    cash -= 101.5 * 60.0 * 3.0 * 1.0e-4;

    const double equity =
        cash + (mm::to_double(ledger.position(0).quantity()) * ledger.last_mid(0));

    CHECK(ledger.breakdown(0).total() == Catch::Approx(equity).margin(1e-9));
}

TEST_CASE("A rejected on_fill leaves the ledger untouched", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0, .maker_fee_bps = 2.0}});
    ledger.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0);
    const mm::PnlBreakdown before = ledger.breakdown(0);

    // A zero quantity is rejected by Position, after the mid was validated:
    // the new mid 110 must not have leaked into last_mid or inventory.
    CHECK_THROWS_AS(ledger.on_fill(0, mm::Side::Sell, 100.0, 0, 110.0), std::invalid_argument);
    CHECK_THROWS_AS(ledger.on_fill(0, mm::Side::Sell, quiet_nan, 1, 110.0), std::invalid_argument);
    CHECK_THROWS_AS(ledger.on_fill(0, mm::Side::Sell, 100.0, 1, quiet_nan), std::invalid_argument);
    CHECK_THROWS_AS(ledger.on_fill(0, mm::Side::Sell, 100.0, 1, infinity), std::invalid_argument);

    CHECK(ledger.last_mid(0) == 100.0);
    CHECK(ledger.position(0).quantity() == 10);
    expect_same(ledger.breakdown(0), before);
}

TEST_CASE("A rejected on_mark leaves the ledger untouched", "[mm][pnl]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    ledger.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0);
    const mm::PnlBreakdown before = ledger.breakdown(0);

    CHECK_THROWS_AS(ledger.on_mark(0, quiet_nan), std::invalid_argument);
    CHECK_THROWS_AS(ledger.on_mark(0, -infinity), std::invalid_argument);

    CHECK(ledger.last_mid(0) == 100.0);
    expect_same(ledger.breakdown(0), before);
}
