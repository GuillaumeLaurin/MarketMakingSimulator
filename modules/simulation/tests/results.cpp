/// \file
/// Tests for simulation/results.hpp: AssetResults' own accumulators behave
/// correctly when fed in the roles results_observer.hpp will use them for
/// (pnl_increments with per-tick CHANGES, equity_drawdown with the running
/// LEVEL), the shared shape between a single asset and the portfolio, and
/// that a default-constructed Results starts genuinely empty.

#include <cmath>
#include <cstdint>
#include <type_traits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/pnl.hpp>

#include <simulation/results.hpp>

namespace sim = mms::simulation;
namespace mm = mms::mm;

TEST_CASE("A default AssetResults has no samples and a zero P&L", "[simulation][results]") {
    const sim::AssetResults results;

    CHECK(results.pnl_increments.count() == 0);
    CHECK(results.equity_drawdown.count() == 0);
    CHECK(results.mm_trade_count == 0);
    CHECK(results.adverse_trade_count == 0);
    CHECK(results.final_breakdown.total() == 0.0);
}

TEST_CASE("A default Results has no assets and a flat portfolio", "[simulation][results]") {
    const sim::Results results;

    CHECK(results.assets.empty());
    CHECK(results.portfolio.mm_trade_count == 0);
}

TEST_CASE("pnl_increments, fed the per-tick CHANGE in total P&L, reports the right mean",
          "[simulation][results]") {
    sim::AssetResults results;

    // Equity went 0 -> 10 -> 4 -> 10: increments +10, -6, +6.
    results.pnl_increments.add(10.0);
    results.pnl_increments.add(-6.0);
    results.pnl_increments.add(6.0);

    CHECK(results.pnl_increments.count() == 3);
    CHECK(results.pnl_increments.mean() == Catch::Approx((10.0 - 6.0 + 6.0) / 3.0));
}

TEST_CASE("equity_drawdown, fed the running P&L LEVEL, tracks peak and drawdown",
          "[simulation][results]") {
    sim::AssetResults results;

    // The same trajectory as above, fed as LEVELS this time: 0, 10, 4, 10.
    results.equity_drawdown.add(0.0);
    results.equity_drawdown.add(10.0);
    results.equity_drawdown.add(4.0);
    results.equity_drawdown.add(10.0);

    CHECK(results.equity_drawdown.peak() == 10.0);
    CHECK(results.equity_drawdown.max_drawdown() == 6.0);     // the dip from 10 to 4
    CHECK(results.equity_drawdown.current_drawdown() == 0.0); // back at the peak
}

TEST_CASE("Trade counts are independent of each other and of the accumulators",
          "[simulation][results]") {
    sim::AssetResults results;

    results.mm_trade_count = 3;
    results.adverse_trade_count = 5;

    CHECK(results.mm_trade_count == 3);
    CHECK(results.adverse_trade_count == 5);
    CHECK(results.pnl_increments.count() == 0);
}

TEST_CASE("final_breakdown holds whatever PnlBreakdown it was assigned", "[simulation][results]") {
    sim::AssetResults results;

    results.final_breakdown = mm::PnlBreakdown{.realized = 10.0,
                                               .unrealized = 5.0,
                                               .spread_capture = 12.0,
                                               .inventory = 3.0,
                                               .fees = -4.0};

    CHECK(results.final_breakdown.total() == 11.0);
}

TEST_CASE("Results holds one AssetResults per asset plus an independent portfolio entry",
          "[simulation][results]") {
    sim::Results results;
    results.assets.resize(2);
    results.assets[0].mm_trade_count = 1;
    results.assets[1].mm_trade_count = 2;
    results.portfolio.mm_trade_count = 3;

    REQUIRE(results.assets.size() == 2);
    CHECK(results.assets[0].mm_trade_count == 1);
    CHECK(results.assets[1].mm_trade_count == 2);
    // The portfolio entry does not derive from or alias the per-asset ones;
    // whatever sums them (results_observer.hpp) must do so explicitly.
    CHECK(results.portfolio.mm_trade_count == 3);
}

TEST_CASE("AssetResults is the same type whether it describes an asset or the portfolio",
          "[simulation][results]") {
    STATIC_REQUIRE(std::is_same_v<decltype(sim::Results::portfolio), sim::AssetResults>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::Results::assets)::value_type, sim::AssetResults>);
}
