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

#include <agents/action.hpp>
#include <agents/reject_reason.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/pnl.hpp>
#include <mm/strategy_config.hpp>
#include <mm/types.hpp>

#include <math/process/dynamics.hpp>
#include <math/rng/seed.hpp>
#include <math/types.hpp>
#include <simulation/agent_spec.hpp>
#include <simulation/results_observer.hpp>
#include <simulation/simulation_config.hpp>
#include <simulation/simulation_observer.hpp>
#include <simulation/tick_loop.hpp>
#include <simulation/world.hpp>

#include "maker.hpp"

namespace sim = mms::simulation;
namespace math = mms::math;
namespace mm = mms::mm;

namespace {

mm::PnlLedger ledger_of(const std::vector<mm::LedgerAsset>& assets) {
    return mm::PnlLedger{assets};
}

std::vector<sim::ObservedAgent> watch(const mm::PnlLedger& ledger) {
    return {{.name = "agent", .ledger = &ledger}};
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

    const sim::ResultsObserver observer(watch(ledger));
    const sim::AgentResults& results = observer.results().agents[0];

    REQUIRE(results.assets.size() == 2);
    for (const sim::AssetResults& asset : results.assets) {
        CHECK(asset.aggressive_trades == 0);
        CHECK(asset.passive_trades == 0);
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

    const sim::ResultsObserver observer(watch(ledger));

    CHECK(observer.results().agents[0].assets[0].equity_drawdown.peak() ==
          Catch::Approx(-20.0).margin(1e-9));
    CHECK(observer.results().agents[0].assets[0].final_breakdown.total() ==
          Catch::Approx(-20.0).margin(1e-9));
    CHECK(observer.results().agents[0].portfolio.equity_drawdown.peak() ==
          Catch::Approx(-20.0).margin(1e-9));
}

// ------------------------------------------------------- ticks: change/level

TEST_CASE("Each tick records the CHANGE in total P&L, and the LEVEL for the drawdown",
          "[simulation][results_observer]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(watch(ledger));
    ledger.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0); // long 10 at the mid: total 0

    ledger.on_mark(0, 103.0); // total +30
    observer.on_tick_end(1.0);
    ledger.on_mark(0, 101.0); // total +10
    observer.on_tick_end(2.0);

    const sim::AssetResults& asset = observer.results().agents[0].assets[0];
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
    sim::ResultsObserver observer(watch(ledger));
    ledger.on_fill(0, mm::Side::Buy, 100.0, 1, 100.0);

    ledger.on_mark(0, 98.0);
    observer.on_tick_end(1.0);
    ledger.on_mark(0, 95.0);
    observer.on_tick_end(2.0);

    CHECK(observer.results().agents[0].assets[0].equity_drawdown.max_drawdown() ==
          Catch::Approx(5.0));
}

TEST_CASE("An idle tick records a zero increment and a level sample",
          "[simulation][results_observer]") {
    const mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(watch(ledger));

    observer.on_tick_end(1.0);
    observer.on_tick_end(2.0);
    observer.on_tick_end(3.0);

    const sim::AssetResults& asset = observer.results().agents[0].assets[0];
    CHECK(asset.pnl_increments.count() == 3);
    CHECK(asset.pnl_increments.mean() == 0.0);
    CHECK(asset.equity_drawdown.count() == 4); // start + 3 ticks
    CHECK(asset.equity_drawdown.max_drawdown() == 0.0);
}

TEST_CASE("The total includes fees, so a fee shows up as a negative increment",
          "[simulation][results_observer]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0, .maker_fee_bps = 2.0}});
    sim::ResultsObserver observer(watch(ledger));

    ledger.on_fill(0, mm::Side::Buy, 100.0, 1000, 100.0); // filled at the mid: only the fee moves
    observer.on_tick_end(1.0);

    CHECK(observer.results().agents[0].assets[0].pnl_increments.mean() ==
          Catch::Approx(-20.0).margin(1e-9));
    CHECK(observer.results().agents[0].assets[0].final_breakdown.fees ==
          Catch::Approx(-20.0).margin(1e-9));
}

TEST_CASE("final_breakdown is the ledger's breakdown at the last tick",
          "[simulation][results_observer]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0, .maker_fee_bps = 1.0}});
    sim::ResultsObserver observer(watch(ledger));
    ledger.on_fill(0, mm::Side::Buy, 99.0, 10, 100.0);
    ledger.on_mark(0, 102.0);
    observer.on_tick_end(1.0);
    ledger.on_fill(0, mm::Side::Sell, 103.0, 4, 102.0);
    ledger.on_mark(0, 101.0);
    observer.on_tick_end(2.0);

    const mm::PnlBreakdown expected = ledger.breakdown(0);
    const mm::PnlBreakdown& actual = observer.results().agents[0].assets[0].final_breakdown;

    CHECK(actual.realized == expected.realized);
    CHECK(actual.unrealized == expected.unrealized);
    CHECK(actual.spread_capture == expected.spread_capture);
    CHECK(actual.inventory == expected.inventory);
    CHECK(actual.fees == expected.fees);
}

TEST_CASE("The portfolio is the sum of the assets, tick by tick",
          "[simulation][results_observer]") {
    mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}});
    sim::ResultsObserver observer(watch(ledger));
    ledger.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0);
    ledger.on_fill(1, mm::Side::Sell, 50.0, 4, 50.0);

    ledger.on_mark(0, 102.0); // asset 0: +20
    ledger.on_mark(1, 49.0);  // asset 1: short 4, +4
    observer.on_tick_end(1.0);
    ledger.on_mark(0, 101.0); // asset 0 now +10
    ledger.on_mark(1, 51.0);  // asset 1 now -4
    observer.on_tick_end(2.0);

    const sim::AgentResults& results = observer.results().agents[0];
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

// ------------------------------------------------------------------- agents

TEST_CASE("Each agent has its own results, named as in its profile, in the order given",
          "[simulation][results_observer]") {
    mm::PnlLedger first = ledger_of({{.initial_mid = 100.0}});
    mm::PnlLedger second = ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}});

    const sim::ResultsObserver observer(
        {{.name = "alice", .ledger = &first}, {.name = "bob", .ledger = &second}});

    const sim::Results& results = observer.results();
    REQUIRE(results.agents.size() == 2);
    CHECK(results.agents[0].name == "alice");
    CHECK(results.agents[0].assets.size() == 1);
    CHECK(results.agents[1].name == "bob");
    CHECK(results.agents[1].assets.size() == 2);
}

TEST_CASE("Each agent is measured against its own ledger", "[simulation][results_observer]") {
    mm::PnlLedger winner = ledger_of({{.initial_mid = 100.0}});
    mm::PnlLedger loser = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(
        {{.name = "winner", .ledger = &winner}, {.name = "loser", .ledger = &loser}});
    winner.on_fill(0, mm::Side::Buy, 100.0, 10, 100.0);
    loser.on_fill(0, mm::Side::Sell, 100.0, 10, 100.0);

    winner.on_mark(0, 103.0);
    loser.on_mark(0, 103.0);
    observer.on_tick_end(1.0);

    CHECK(observer.results().agents[0].portfolio.final_breakdown.total() == Catch::Approx(30.0));
    CHECK(observer.results().agents[1].portfolio.final_breakdown.total() == Catch::Approx(-30.0));
    CHECK(observer.results().agents[1].portfolio.equity_drawdown.max_drawdown() ==
          Catch::Approx(30.0));
}

TEST_CASE("A null ledger is refused", "[simulation][results_observer]") {
    CHECK_THROWS_AS(sim::ResultsObserver({{.name = "nobody", .ledger = nullptr}}),
                    std::invalid_argument);
}

TEST_CASE("An observer with no agent has no results and ticks harmlessly",
          "[simulation][results_observer]") {
    sim::ResultsObserver observer({});

    observer.on_tick_end(1.0);

    CHECK(observer.results().agents.empty());
}

// ------------------------------------------------------------------- trades

TEST_CASE("A trade is counted for the agent that started it and for the one that was hit",
          "[simulation][results_observer]") {
    mm::PnlLedger a = ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}});
    mm::PnlLedger b = ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}});
    mm::PnlLedger c = ledger_of({{.initial_mid = 100.0}, {.initial_mid = 50.0}});
    sim::ResultsObserver observer(
        {{.name = "a", .ledger = &a}, {.name = "b", .ledger = &b}, {.name = "c", .ledger = &c}});

    // a (buyer) took what b was offering, twice on asset 0; c (seller) hit a's bid on asset 1.
    observer.on_trade(0, some_trade(), {.buyer = 0, .seller = 1, .aggressor = mm::Side::Buy});
    observer.on_trade(0, some_trade(), {.buyer = 0, .seller = 1, .aggressor = mm::Side::Buy});
    observer.on_trade(1, some_trade(), {.buyer = 0, .seller = 2, .aggressor = mm::Side::Sell});

    const sim::Results& results = observer.results();
    CHECK(results.agents[0].assets[0].aggressive_trades == 2);
    CHECK(results.agents[0].assets[0].passive_trades == 0);
    CHECK(results.agents[0].assets[1].aggressive_trades == 0);
    CHECK(results.agents[0].assets[1].passive_trades == 1);
    CHECK(results.agents[0].portfolio.aggressive_trades == 2);
    CHECK(results.agents[0].portfolio.passive_trades == 1);

    CHECK(results.agents[1].assets[0].passive_trades == 2);
    CHECK(results.agents[1].portfolio.passive_trades == 2);
    CHECK(results.agents[1].portfolio.aggressive_trades == 0);

    CHECK(results.agents[2].assets[1].aggressive_trades == 1);
    CHECK(results.agents[2].portfolio.aggressive_trades == 1);
    CHECK(results.agents[2].portfolio.passive_trades == 0);
}

TEST_CASE("A trade on an unknown asset or between unknown agents throws std::out_of_range and "
          "counts nothing",
          "[simulation][results_observer]") {
    mm::PnlLedger a = ledger_of({{.initial_mid = 100.0}});
    mm::PnlLedger b = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer({{.name = "a", .ledger = &a}, {.name = "b", .ledger = &b}});

    CHECK_THROWS_AS(
        observer.on_trade(1, some_trade(), {.buyer = 0, .seller = 1, .aggressor = mm::Side::Buy}),
        std::out_of_range);
    CHECK_THROWS_AS(
        observer.on_trade(0, some_trade(), {.buyer = 0, .seller = 2, .aggressor = mm::Side::Buy}),
        std::out_of_range);
    CHECK_THROWS_AS(
        observer.on_trade(0, some_trade(), {.buyer = 3, .seller = 1, .aggressor = mm::Side::Sell}),
        std::out_of_range);

    // The one that was looked up first is not left half-counted.
    CHECK(observer.results().agents[0].portfolio.aggressive_trades == 0);
    CHECK(observer.results().agents[1].portfolio.passive_trades == 0);
    CHECK(observer.results().agents[1].portfolio.aggressive_trades == 0);
}

// ----------------------------------------------------------------- refusals

TEST_CASE("A refusal is counted for the agent under its reason", "[simulation][results_observer]") {
    mm::PnlLedger a = ledger_of({{.initial_mid = 100.0}});
    mm::PnlLedger b = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer({{.name = "a", .ledger = &a}, {.name = "b", .ledger = &b}});
    const mms::agents::Action action =
        mms::agents::SubmitMarket{.asset = 0, .side = mm::Side::Buy, .quantity = 1};

    observer.on_reject(1, action, mms::agents::RejectReason::InsufficientCash);
    observer.on_reject(1, action, mms::agents::RejectReason::InsufficientCash);
    observer.on_reject(1, action, mms::agents::RejectReason::PositionLimit);

    const sim::AgentResults& refused = observer.results().agents[1];
    CHECK(refused.rejects[static_cast<std::size_t>(mms::agents::RejectReason::InsufficientCash)] ==
          2);
    CHECK(refused.rejects[static_cast<std::size_t>(mms::agents::RejectReason::PositionLimit)] == 1);
    CHECK(refused.rejects[static_cast<std::size_t>(mms::agents::RejectReason::InvalidOrder)] == 0);
    CHECK(refused.total_rejects() == 3);
    CHECK(observer.results().agents[0].total_rejects() == 0);
}

TEST_CASE("Every reason has its own counter", "[simulation][results_observer]") {
    mm::PnlLedger a = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(watch(a));
    const mms::agents::Action action = mms::agents::Cancel{.asset = 0, .id = mm::OrderId{1}};

    for (std::size_t reason = 0; reason < mms::agents::reject_reason_count; ++reason) {
        for (std::size_t times = 0; times <= reason; ++times) {
            observer.on_reject(0, action, static_cast<mms::agents::RejectReason>(reason));
        }
    }

    for (std::size_t reason = 0; reason < mms::agents::reject_reason_count; ++reason) {
        CHECK(observer.results().agents[0].rejects[reason] ==
              static_cast<std::int64_t>(reason + 1));
    }
}

TEST_CASE("A refusal by an unknown agent throws std::out_of_range",
          "[simulation][results_observer]") {
    mm::PnlLedger a = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(watch(a));

    CHECK_THROWS_AS(observer.on_reject(1,
                                       mms::agents::Cancel{.asset = 0, .id = mm::OrderId{1}},
                                       mms::agents::RejectReason::InvalidOrder),
                    std::out_of_range);
}

// ------------------------------------------------------------------- others

TEST_CASE("The other events change nothing", "[simulation][results_observer]") {
    const mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(watch(ledger));

    observer.on_tick_start(1.0);
    observer.on_mark(0, 123.0);

    CHECK(observer.results().agents[0].assets[0].pnl_increments.count() == 0);
    CHECK(observer.results().agents[0].assets[0].equity_drawdown.count() == 1);
    CHECK(observer.results().agents[0].assets[0].aggressive_trades == 0);
}

TEST_CASE("results() is the live state, the same object at every call",
          "[simulation][results_observer]") {
    const mm::PnlLedger ledger = ledger_of({{.initial_mid = 100.0}});
    sim::ResultsObserver observer(watch(ledger));
    const sim::Results& first = observer.results();

    observer.on_tick_end(1.0);

    CHECK(&first == &observer.results());
    CHECK(first.agents[0].assets[0].pnl_increments.count() == 1);
}

// -------------------------------------------------------------- integration

namespace {

/// Counts the trades a run notifies, independently of ResultsObserver.
struct TradeCounter final : sim::SimulationObserver {
    std::vector<std::int64_t> aggressive;
    std::vector<std::int64_t> passive;
    std::vector<std::int64_t> rejected;

    explicit TradeCounter(std::size_t agents)
        : aggressive(agents, 0), passive(agents, 0), rejected(agents, 0) {}

    void on_trade(mm::AssetId /*asset*/,
                  const mm::Trade& /*trade*/,
                  const sim::TradeParties& parties) override {
        ++aggressive[parties.aggressive()];
        ++passive[parties.passive()];
    }

    void on_reject(sim::AgentId agent,
                   const mms::agents::Action& /*action*/,
                   mms::agents::RejectReason /*reason*/) override {
        ++rejected[agent];
    }
};

sim::SimulationConfig two_asset_config() {
    std::vector<sim::AssetSpec> assets;
    for (const double mid : {100.0, 150.0}) {
        assets.push_back(sim::AssetSpec{.symbol = "ASSET" + std::to_string(static_cast<int>(mid)),
                                        .initial_mid = mid,
                                        .maker_fee_bps = 1.0,
                                        .price_volatility = 1.0,
                                        .mu = 0.0,
                                        .factor_model = std::nullopt});
    }
    sim::SimulationConfig config{
        .assets = std::move(assets),
        .correlation = math::Matrix::Identity(2, 2),
        .process = sim::RandomWalkSpec{.dynamics = mms::math::process::Dynamics::Arithmetic},
        .master_seed = math::rng::Seed{11},
        .dt = 1.0,
        .tick_count = 40};
    config.agents = {
        sim::tests::informed_agent(3.0, 0.5),
        sim::tests::maker_agent(mm::StrategyConfig{
            .gamma = 2.0,
            .k = math::Vector::Constant(2, 1.0),
            .correlation = math::Matrix::Identity(2, 2),
            .estimators = std::vector<mm::EstimatorSpec>(2, mm::ConstantSpec{.sigma = 0.2}),
            .horizon = mm::ConstantHorizon{.tau = 1.0},
            .quote_sizes = std::vector<mm::Quantity>(2, 10),
            .tick_sizes = std::vector<mm::Price>(2, 0.01),
            .limits = {.max_abs_position = std::vector<mm::Quantity>(2, 1000),
                       .max_portfolio_risk = std::nullopt},
            .min_samples = 0}),
        // Too poor to buy anything: every order it tries is refused.
        sim::AgentSpec{.profile = {.name = "broke", .initial_cash = 0.0},
                       .timing = {},
                       .behavior = mms::agents::InformedFlowSpec{
                           .lambda = 2.0,
                           .direction_correlation = 0.0,
                           .size = sim::UniformIntSpec{.min = 1, .max = 3}}}};
    return config;
}

} // namespace

TEST_CASE("Over a real run the results satisfy the identities that must always hold",
          "[simulation][results_observer]") {
    const sim::SimulationConfig config = two_asset_config();
    sim::World world(config);
    sim::TickLoop loop(world, config);
    sim::ResultsObserver observer(sim::observed_agents(world));
    TradeCounter counter(world.agent_count());
    loop.add_observer(observer);
    loop.add_observer(counter);

    for (std::int64_t tick = 0; tick < config.tick_count; ++tick) {
        loop.step();
    }

    const sim::Results& results = observer.results();
    REQUIRE(results.agents.size() == 3);
    CHECK(results.agents[0].name == "informed");
    CHECK(results.agents[1].name == "maker");
    CHECK(results.agents[2].name == "broke");

    for (sim::AgentId id = 0; id < results.agents.size(); ++id) {
        const sim::AgentResults& agent = results.agents[id];
        const mm::PnlLedger& ledger = world.agent(id).account().ledger();
        const sim::AssetResults& portfolio = agent.portfolio;
        INFO("agent " << agent.name);

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
              Catch::Approx(agent.assets[0].final_breakdown.total() +
                            agent.assets[1].final_breakdown.total())
                  .margin(1e-6));
        CHECK(agent.assets[0].aggressive_trades + agent.assets[1].aggressive_trades ==
              portfolio.aggressive_trades);
        CHECK(agent.assets[0].passive_trades + agent.assets[1].passive_trades ==
              portfolio.passive_trades);

        // The counts agree with a counter that knows nothing about Results.
        CHECK(portfolio.aggressive_trades == counter.aggressive[id]);
        CHECK(portfolio.passive_trades == counter.passive[id]);
        CHECK(agent.total_rejects() == counter.rejected[id]);
    }

    // Every trade has one agent that started it and one that was hit.
    std::int64_t aggressive = 0;
    std::int64_t passive = 0;
    for (const sim::AgentResults& agent : results.agents) {
        aggressive += agent.portfolio.aggressive_trades;
        passive += agent.portfolio.passive_trades;
    }
    CHECK(aggressive == passive);
    CHECK(aggressive > 0);

    // The informed trader only ever takes, the maker only ever waits to be hit.
    CHECK(results.agents[0].portfolio.aggressive_trades > 0);
    CHECK(results.agents[1].portfolio.passive_trades > 0);
    // And the one without cash was refused, and traded nothing.
    CHECK(results.agents[2].total_rejects() > 0);
    CHECK(results.agents[2].portfolio.aggressive_trades == 0);
    CHECK(results.agents[2]
              .rejects[static_cast<std::size_t>(mms::agents::RejectReason::InsufficientCash)] > 0);
}
