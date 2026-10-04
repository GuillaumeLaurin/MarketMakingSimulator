/// \file
/// Tests for simulation/results_observer.hpp. Most of them drive the observer
/// by hand against a bare PnlLedger, so every number is exact and checkable
/// without a World: the tick's CHANGE in total P&L goes to the running
/// statistics, the LEVEL goes to the drawdown tracker, the history starts
/// from the ledger's level at construction, fees are included, and trades
/// are counted by aggressor. One integration test then runs a real
/// simulation and checks the identities that must hold whatever happened.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/pnl.hpp>
#include <mm/strategy_config.hpp>
#include <mm/types.hpp>

#include <math/rng/seed.hpp>
#include <math/types.hpp>
#include <simulation/results_observer.hpp>
#include <simulation/simulation_config.hpp>
#include <simulation/simulation_observer.hpp>
#include <simulation/tick_loop.hpp>
#include <simulation/world.hpp>

namespace sim = mms::simulation;
namespace math = mms::math;
namespace mm = mms::mm;

namespace {

mm::PnlLedger ledger_of(const std::vector<mm::LedgerAsset>& assets) {
    return mm::PnlLedger{assets};
}

mm::Trade some_trade() {
    return mm::Trade{.buy_order_id = mm::OrderId{1},
                     .sell_order_id = mm::OrderId{2},
                     .price = 100.0,
                     .quantity = 1};
}

} // namespace

// ------------------------------------------------------------- construction

TEST_CASE("A fresh observer has one empty result per asset and one starting sample",
          "[simulation][results_observer]") {
    const mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}});

    const sim::ResultsObserver observer(ledger);
    const sim::Results& results = observer.results();

    REQUIRE(results.assets.size() == 2);
    for (const sim::AssetResults& asset : results.assets) {
        CHECK(asset.mm_trade_count == 0);
        CHECK(asset.adverse_trade_count == 0);
        CHECK(asset.pnl_increments.count() == 0);
        CHECK(asset.equity_drawdown.count() == 1); // the starting level
    }
    CHECK(results.portfolio.pnl_increments.count() == 0);
    CHECK(results.portfolio.equity_drawdown.count() == 1);
}

TEST_CASE("The drawdown history starts from the ledger's level at construction, not from zero",
          "[simulation][results_observer]") {
    // A fee already paid before the observer exists: the ledger starts below 0.
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0, .maker_fee_bps = 2.0}});
    ledger.on_fill(0, mm::Side::Buy, 100.0, 1000, 100.0); // notional 100000 at 2 bps: -20
    REQUIRE(ledger.breakdown(0).total() == Catch::Approx(-20.0).margin(1e-9));

    const sim::ResultsObserver observer(ledger);

    CHECK(observer.results().assets[0].equity_drawdown.peak() == Catch::Approx(-20.0).margin(1e-9));
    CHECK(observer.results().assets[0].final_breakdown.total() ==
          Catch::Approx(-20.0).margin(1e-9));
    CHECK(observer.results().portfolio.equity_drawdown.peak() == Catch::Approx(-20.0).margin(1e-9));
}

// ------------------------------------------------------- ticks: change/level

TEST_CASE("Each tick records the CHANGE in total P&L, and the LEVEL for the drawdown",
          "[simulation][results_observer]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(ledger);
    ledger.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0); // long 10 at the mid: total 0

    ledger.on_mark(0, 103.0); // total +30
    observer.on_tick_end(1.0);
    ledger.on_mark(0, 101.0); // total +10
    observer.on_tick_end(2.0);

    const sim::AssetResults& asset = observer.results().assets[0];
    // Increments +30 then -20.
    CHECK(asset.pnl_increments.count() == 2);
    CHECK(asset.pnl_increments.mean() == Catch::Approx(5.0));
    CHECK(asset.pnl_increments.max() == Catch::Approx(30.0));
    CHECK(asset.pnl_increments.min() == Catch::Approx(-20.0));
    // Levels 0 (start), 30, 10: peak 30, deepest dip 20, still 20 under it.
    CHECK(asset.equity_drawdown.count() == 3);
    CHECK(asset.equity_drawdown.peak() == Catch::Approx(30.0));
    CHECK(asset.equity_drawdown.max_drawdown() == Catch::Approx(20.0));
    CHECK(asset.equity_drawdown.current_drawdown() == Catch::Approx(20.0));
}

TEST_CASE("A run that only ever loses is measured from where it began",
          "[simulation][results_observer]") {
    // Levels 0, -2, -5: without the starting sample the first level seen
    // would be -2 and the drawdown would read 3 instead of 5.
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(ledger);
    ledger.on_fill(0, mm::Side::Buy, 100.0, 1, 100.0);

    ledger.on_mark(0, 98.0);
    observer.on_tick_end(1.0);
    ledger.on_mark(0, 95.0);
    observer.on_tick_end(2.0);

    CHECK(observer.results().assets[0].equity_drawdown.max_drawdown() == Catch::Approx(5.0));
}

TEST_CASE("An idle tick records a zero increment and a level sample",
          "[simulation][results_observer]") {
    const mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(ledger);

    observer.on_tick_end(1.0);
    observer.on_tick_end(2.0);
    observer.on_tick_end(3.0);

    const sim::AssetResults& asset = observer.results().assets[0];
    CHECK(asset.pnl_increments.count() == 3);
    CHECK(asset.pnl_increments.mean() == 0.0);
    CHECK(asset.equity_drawdown.count() == 4); // start + 3 ticks
    CHECK(asset.equity_drawdown.max_drawdown() == 0.0);
}

TEST_CASE("The total includes fees, so a fee shows up as a negative increment",
          "[simulation][results_observer]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0, .maker_fee_bps = 2.0}});
    sim::ResultsObserver observer(ledger);

    ledger.on_fill(0, mm::Side::Buy, 100.0, 1000, 100.0); // filled at the mid: only the fee moves
    observer.on_tick_end(1.0);

    CHECK(observer.results().assets[0].pnl_increments.mean() == Catch::Approx(-20.0).margin(1e-9));
    CHECK(observer.results().assets[0].final_breakdown.fees == Catch::Approx(-20.0).margin(1e-9));
}

TEST_CASE("final_breakdown is the ledger's breakdown at the last tick",
          "[simulation][results_observer]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0, .maker_fee_bps = 1.0}});
    sim::ResultsObserver observer(ledger);
    ledger.on_fill(0, mm::Side::Buy, 99.0, 10, 100.0);
    ledger.on_mark(0, 102.0);
    observer.on_tick_end(1.0);
    ledger.on_fill(0, mm::Side::Sell, 103.0, 4, 102.0);
    ledger.on_mark(0, 101.0);
    observer.on_tick_end(2.0);

    const mm::PnlBreakdown expected = ledger.breakdown(0);
    const mm::PnlBreakdown& actual = observer.results().assets[0].final_breakdown;

    CHECK(actual.realized == expected.realized);
    CHECK(actual.unrealized == expected.unrealized);
    CHECK(actual.spread_capture == expected.spread_capture);
    CHECK(actual.inventory == expected.inventory);
    CHECK(actual.fees == expected.fees);
}

TEST_CASE("The portfolio is the sum of the assets, tick by tick",
          "[simulation][results_observer]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}});
    sim::ResultsObserver observer(ledger);
    ledger.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0);
    ledger.on_fill(1, mm::Side::Sell, 50.0, 4, 50.0);

    ledger.on_mark(0, 102.0); // asset 0: +20
    ledger.on_mark(1, 49.0);  // asset 1: short 4, +4
    observer.on_tick_end(1.0);
    ledger.on_mark(0, 101.0); // asset 0 now +10
    ledger.on_mark(1, 51.0);  // asset 1 now -4
    observer.on_tick_end(2.0);

    const sim::Results& results = observer.results();
    CHECK(results.portfolio.pnl_increments.count() == 2);
    CHECK(results.portfolio.pnl_increments.mean() ==
          Catch::Approx(results.assets[0].pnl_increments.mean() +
                        results.assets[1].pnl_increments.mean()));
    CHECK(results.portfolio.final_breakdown.total() ==
          Catch::Approx(results.assets[0].final_breakdown.total() +
                        results.assets[1].final_breakdown.total()));
    CHECK(results.portfolio.final_breakdown.total() == Catch::Approx(6.0)); // +10 and -4
    // Each asset keeps its own history, independent of the other.
    CHECK(results.assets[0].equity_drawdown.peak() == Catch::Approx(20.0));
    CHECK(results.assets[1].equity_drawdown.peak() == Catch::Approx(4.0));
}

// ------------------------------------------------------------------- trades

TEST_CASE("Trades are counted by aggressor, per asset and for the portfolio",
          "[simulation][results_observer]") {
    const mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}});
    sim::ResultsObserver observer(ledger);

    observer.on_trade(0, some_trade(), false); // adverse flow on asset 0
    observer.on_trade(0, some_trade(), false);
    observer.on_trade(0, some_trade(), true); // the market maker crossed its own quote
    observer.on_trade(1, some_trade(), false);

    const sim::Results& results = observer.results();
    CHECK(results.assets[0].adverse_trade_count == 2);
    CHECK(results.assets[0].mm_trade_count == 1);
    CHECK(results.assets[1].adverse_trade_count == 1);
    CHECK(results.assets[1].mm_trade_count == 0);
    CHECK(results.portfolio.adverse_trade_count == 3);
    CHECK(results.portfolio.mm_trade_count == 1);
}

TEST_CASE("A trade on an unknown asset throws std::out_of_range",
          "[simulation][results_observer]") {
    const mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(ledger);

    CHECK_THROWS_AS(observer.on_trade(1, some_trade(), false), std::out_of_range);
    CHECK(observer.results().portfolio.adverse_trade_count == 0); // nothing was half-counted
}

// ------------------------------------------------------------------- others

TEST_CASE("The other events change nothing", "[simulation][results_observer]") {
    const mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(ledger);

    observer.on_tick_start(1.0);
    observer.on_mark(0, 123.0);
    observer.on_quote(0, mm::AssetQuotes{});

    CHECK(observer.results().assets[0].pnl_increments.count() == 0);
    CHECK(observer.results().assets[0].equity_drawdown.count() == 1);
    CHECK(observer.results().assets[0].mm_trade_count == 0);
}

TEST_CASE("results() is the live state, the same object at every call",
          "[simulation][results_observer]") {
    const mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(ledger);
    const sim::Results& first = observer.results();

    observer.on_tick_end(1.0);

    CHECK(&first == &observer.results());
    CHECK(first.assets[0].pnl_increments.count() == 1);
}

// -------------------------------------------------------------- integration

namespace {

/// Counts the trades a run notifies, independently of ResultsObserver.
struct TradeCounter final : sim::SimulationObserver {
    std::int64_t mm_aggressor = 0;
    std::int64_t adverse_aggressor = 0;

    void
    on_trade(mm::AssetId /*asset*/, const mm::Trade& /*trade*/, bool aggressor_is_mm) override {
        (aggressor_is_mm ? mm_aggressor : adverse_aggressor) += 1;
    }
};

sim::SimulationConfig two_asset_config() {
    std::vector<sim::AssetSpec> assets;
    for (const double mid : {100.0, 150.0}) {
        assets.push_back(
            sim::AssetSpec{.symbol = "ASSET" + std::to_string(static_cast<int>(mid)),
                           .initial_mid = mid,
                           .maker_fee_bps = 1.0,
                           .price_volatility = 1.0,
                           .mu = 0.0,
                           .factor_model = std::nullopt,
                           .adverse_flow_lambda = 3.0,
                           .adverse_flow_direction_correlation = 0.5,
                           .adverse_flow_size = sim::UniformIntSpec{.min = 1, .max = 5}});
    }
    return sim::SimulationConfig{
        .assets = std::move(assets),
        .correlation = math::Matrix::Identity(2, 2),
        .process = sim::RandomWalkSpec{},
        .strategy =
            mm::StrategyConfig{
                .gamma = 2.0,
                .k = math::Vector::Constant(2, 1.0),
                .correlation = math::Matrix::Identity(2, 2),
                .estimators = std::vector<mm::EstimatorSpec>(2, mm::ConstantSpec{.sigma = 0.2}),
                .horizon = mm::ConstantHorizon{.tau = 1.0},
                .quote_sizes = std::vector<mm::Quantity>(2, 10),
                .tick_sizes = std::vector<mm::Price>(2, 0.01),
                .limits = {.max_abs_position = std::vector<mm::Quantity>(2, 1000),
                           .max_portfolio_risk = std::nullopt},
                .min_samples = 0},
        .latency = {.quote_delay = sim::ConstantDelaySpec{.delay = 0.0},
                    .adverse_flow_delay = sim::ConstantDelaySpec{.delay = 0.0}},
        .requote_threshold_ticks = 1,
        .master_seed = math::rng::Seed{11},
        .dt = 1.0,
        .tick_count = 40};
}

} // namespace

TEST_CASE("Over a real run the results satisfy the identities that must always hold",
          "[simulation][results_observer]") {
    const sim::SimulationConfig config = two_asset_config();
    sim::World world(config);
    sim::TickLoop loop(world, config);
    sim::ResultsObserver observer(world.pnl_ledger());
    TradeCounter counter;
    loop.add_observer(observer);
    loop.add_observer(counter);

    for (std::int64_t tick = 0; tick < config.tick_count; ++tick) {
        loop.step();
    }

    const sim::Results& results = observer.results();
    const mm::PnlLedger& ledger = world.pnl_ledger();
    const sim::AssetResults& portfolio = results.portfolio;

    // One increment per tick, one level per tick plus the starting one.
    CHECK(portfolio.pnl_increments.count() == config.tick_count);
    CHECK(portfolio.equity_drawdown.count() == config.tick_count + 1);

    // The increments telescope: their sum is exactly where the P&L ended.
    CHECK(portfolio.pnl_increments.mean() * static_cast<double>(config.tick_count) ==
          Catch::Approx(ledger.portfolio().total()).margin(1e-6));

    // The final breakdown is the ledger's, and the last drawdown sample is its total.
    CHECK(portfolio.final_breakdown.total() == ledger.portfolio().total());
    CHECK(portfolio.equity_drawdown.peak() - portfolio.equity_drawdown.current_drawdown() ==
          Catch::Approx(ledger.portfolio().total()).margin(1e-6));
    CHECK(portfolio.equity_drawdown.max_drawdown() >= 0.0);

    // The portfolio is the sum of its assets.
    CHECK(portfolio.final_breakdown.total() ==
          Catch::Approx(results.assets[0].final_breakdown.total() +
                        results.assets[1].final_breakdown.total())
              .margin(1e-6));

    // The trade counts agree with a counter that knows nothing about Results.
    CHECK(portfolio.adverse_trade_count == counter.adverse_aggressor);
    CHECK(portfolio.mm_trade_count == counter.mm_aggressor);
    CHECK(portfolio.adverse_trade_count > 0); // adverse flow really traded
    CHECK(results.assets[0].adverse_trade_count + results.assets[1].adverse_trade_count ==
          portfolio.adverse_trade_count);
}
