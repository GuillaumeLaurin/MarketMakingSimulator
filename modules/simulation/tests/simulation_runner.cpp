/// \file
/// Tests for simulation/simulation_runner.hpp. The runner is thin glue, so
/// these tests pin what the glue promises: the tick budget (`finished`,
/// `step` past the end, `run` as a no-op once finished), the validation order,
/// that stepping and running give the same run, that extra observers watch
/// the same run AFTER the built-in one, and that run_simulation is the same
/// computation as a runner driven by hand.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/strategy_config.hpp>
#include <mm/types.hpp>

#include <math/process/dynamics.hpp>
#include <math/rng/seed.hpp>
#include <math/types.hpp>
#include <simulation/simulation_runner.hpp>

#include "maker.hpp"

namespace sim = mms::simulation;
namespace math = mms::math;
namespace mm = mms::mm;

static_assert(!std::is_copy_constructible_v<sim::SimulationRunner>);
static_assert(!std::is_copy_assignable_v<sim::SimulationRunner>);
static_assert(!std::is_move_constructible_v<sim::SimulationRunner>);
static_assert(!std::is_move_assignable_v<sim::SimulationRunner>);

namespace {

sim::SimulationConfig make_config(std::int64_t tick_count, std::uint64_t seed = 11) {
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
        .process = sim::RandomWalkSpec{.dynamics = mms::math::process::Dynamics::Arithmetic},
        .latency = {.adverse_flow_delay = sim::ConstantDelaySpec{.delay = 0.0}},
        .agents = {sim::tests::maker_agent(mm::StrategyConfig{
            .gamma = 2.0,
            .k = math::Vector::Constant(2, 1.0),
            .correlation = math::Matrix::Identity(2, 2),
            .estimators = std::vector<mm::EstimatorSpec>(2, mm::ConstantSpec{.sigma = 0.2}),
            .horizon = mm::ConstantHorizon{.tau = 1.0},
            .quote_sizes = std::vector<mm::Quantity>(2, 10),
            .tick_sizes = std::vector<mm::Price>(2, 0.01),
            .limits = {.max_abs_position = std::vector<mm::Quantity>(2, 1000),
                       .max_portfolio_risk = std::nullopt},
            .min_samples = 0})},
        .master_seed = math::rng::Seed{seed},
        .dt = 1.0,
        .tick_count = tick_count};
}

/// Exact equality of everything a run produces for one asset or the portfolio.
bool same(const sim::AssetResults& a, const sim::AssetResults& b) {
    return a.pnl_increments.count() == b.pnl_increments.count() &&
           a.pnl_increments.mean() == b.pnl_increments.mean() &&
           a.pnl_increments.min() == b.pnl_increments.min() &&
           a.pnl_increments.max() == b.pnl_increments.max() &&
           a.equity_drawdown.count() == b.equity_drawdown.count() &&
           a.equity_drawdown.peak() == b.equity_drawdown.peak() &&
           a.equity_drawdown.max_drawdown() == b.equity_drawdown.max_drawdown() &&
           a.mm_trade_count == b.mm_trade_count && a.adverse_trade_count == b.adverse_trade_count &&
           a.final_breakdown.total() == b.final_breakdown.total();
}

bool same(const sim::Results& a, const sim::Results& b) {
    if (a.assets.size() != b.assets.size() || !same(a.portfolio, b.portfolio)) {
        return false;
    }
    for (std::size_t i = 0; i < a.assets.size(); ++i) {
        if (!same(a.assets[i], b.assets[i])) {
            return false;
        }
    }
    return true;
}

/// Records what a second observer sees of the run, including the built-in
/// observer's progress at the moment it is called.
struct Probe final : sim::SimulationObserver {
    explicit Probe(const sim::SimulationRunner& runner) : runner_(&runner) {}

    void on_tick_end(double /*time*/) override {
        ++ticks_seen;
        results_ticks_seen.push_back(runner_->results().portfolio.pnl_increments.count());
    }

    void
    on_trade(mm::AssetId /*asset*/, const mm::Trade& /*trade*/, bool aggressor_is_mm) override {
        (aggressor_is_mm ? mm_aggressor : adverse_aggressor) += 1;
    }

    const sim::SimulationRunner* runner_;
    std::int64_t ticks_seen = 0;
    std::vector<std::int64_t> results_ticks_seen;
    std::int64_t mm_aggressor = 0;
    std::int64_t adverse_aggressor = 0;
};

} // namespace

// ------------------------------------------------------------- construction

TEST_CASE("A fresh runner has run nothing and is not finished", "[simulation][runner]") {
    const sim::SimulationRunner runner(make_config(10));

    CHECK_FALSE(runner.finished());
    CHECK(runner.tick_count() == 10);
    CHECK(runner.loop().tick_count() == 0);
    CHECK(runner.world().asset_count() == 2);
    CHECK(runner.results().assets.size() == 2);
    CHECK(runner.results().portfolio.pnl_increments.count() == 0);
}

TEST_CASE("A negative tick_count throws std::invalid_argument", "[simulation][runner]") {
    CHECK_THROWS_AS(sim::SimulationRunner(make_config(-1)), std::invalid_argument);
}

TEST_CASE("The tick_count is validated before anything else is built", "[simulation][runner]") {
    // An empty basket would make World throw invalid_argument too; the point is
    // that the negative count is what is reported when both are wrong. Both are
    // invalid_argument, so this pins that the runner never reaches a state
    // where it exists with a negative budget.
    sim::SimulationConfig config = make_config(-5);
    config.assets.clear();

    CHECK_THROWS_AS(sim::SimulationRunner(config), std::invalid_argument);
}

TEST_CASE("Anything World or TickLoop reject propagates out of the constructor",
          "[simulation][runner]") {
    sim::SimulationConfig no_assets = make_config(10);
    no_assets.assets.clear();
    CHECK_THROWS_AS(sim::SimulationRunner(no_assets), std::invalid_argument);

    sim::SimulationConfig bad_dt = make_config(10);
    bad_dt.dt = 0.0;
    CHECK_THROWS_AS(sim::SimulationRunner(bad_dt), std::invalid_argument);
}

TEST_CASE("A zero tick_count is a finished run with the starting results", "[simulation][runner]") {
    sim::SimulationRunner runner(make_config(0));

    CHECK(runner.finished());
    CHECK_THROWS_AS(runner.step(), std::logic_error);

    const sim::Results& results = runner.run();
    CHECK(&results == &runner.results());
    CHECK(results.portfolio.pnl_increments.count() == 0);
    CHECK(results.portfolio.equity_drawdown.count() == 1); // the starting level
    CHECK(results.portfolio.final_breakdown.total() == 0.0);
    CHECK(runner.loop().tick_count() == 0);
}

// ------------------------------------------------------------------ stepping

TEST_CASE("step advances one tick and finished flips exactly at the budget",
          "[simulation][runner]") {
    sim::SimulationRunner runner(make_config(3));

    for (std::int64_t tick = 1; tick <= 3; ++tick) {
        CHECK_FALSE(runner.finished());
        runner.step();
        CHECK(runner.loop().tick_count() == tick);
        CHECK(runner.results().portfolio.pnl_increments.count() == tick);
    }
    CHECK(runner.finished());
    CHECK(runner.loop().time() == Catch::Approx(3.0)); // dt = 1
}

TEST_CASE("Stepping past the end throws and changes nothing", "[simulation][runner]") {
    sim::SimulationRunner runner(make_config(4));
    runner.run();
    const sim::Results before = runner.results();

    CHECK_THROWS_AS(runner.step(), std::logic_error);

    CHECK(runner.loop().tick_count() == 4);
    CHECK(same(runner.results(), before));
    CHECK(runner.finished());
}

// ------------------------------------------------------------------- running

TEST_CASE("run executes every tick and returns the live results", "[simulation][runner]") {
    sim::SimulationRunner runner(make_config(25));

    const sim::Results& results = runner.run();

    CHECK(&results == &runner.results());
    CHECK(runner.finished());
    CHECK(runner.loop().tick_count() == 25);
    CHECK(results.portfolio.pnl_increments.count() == 25);
    CHECK(results.portfolio.equity_drawdown.count() == 26);
    CHECK(results.portfolio.adverse_trade_count > 0); // the run really traded
}

TEST_CASE("run once finished does nothing", "[simulation][runner]") {
    sim::SimulationRunner runner(make_config(12));
    runner.run();
    const sim::Results before = runner.results();

    runner.run();

    CHECK(runner.loop().tick_count() == 12);
    CHECK(same(runner.results(), before));
}

TEST_CASE("run after some steps executes only the remaining ticks", "[simulation][runner]") {
    sim::SimulationRunner runner(make_config(20));
    for (int tick = 0; tick < 7; ++tick) {
        runner.step();
    }

    runner.run();

    CHECK(runner.loop().tick_count() == 20);
    CHECK(runner.results().portfolio.pnl_increments.count() == 20);
}

TEST_CASE("Stepping by hand and running give exactly the same run", "[simulation][runner]") {
    sim::SimulationRunner stepped(make_config(30));
    while (!stepped.finished()) {
        stepped.step();
    }
    sim::SimulationRunner ran(make_config(30));
    ran.run();

    CHECK(same(stepped.results(), ran.results()));
}

// -------------------------------------------------------------- determinism

TEST_CASE("The same config gives the same run, another seed another run", "[simulation][runner]") {
    sim::SimulationRunner first(make_config(40, 11));
    sim::SimulationRunner second(make_config(40, 11));
    sim::SimulationRunner other(make_config(40, 12));

    first.run();
    second.run();
    other.run();

    CHECK(same(first.results(), second.results()));
    CHECK_FALSE(same(first.results(), other.results()));
}

TEST_CASE("run_simulation is the same computation as a runner driven by hand",
          "[simulation][runner]") {
    const sim::SimulationConfig config = make_config(35);
    sim::SimulationRunner runner(config);
    runner.run();

    const sim::Results results = sim::run_simulation(config);

    CHECK(same(results, runner.results()));
}

TEST_CASE("run_simulation propagates what the constructor rejects", "[simulation][runner]") {
    CHECK_THROWS_AS(sim::run_simulation(make_config(-1)), std::invalid_argument);
}

TEST_CASE("Runners are independent of each other", "[simulation][runner]") {
    // Two runners alive at once, advanced in alternation, give the same
    // results as each run alone: nothing is shared between them.
    sim::SimulationRunner a(make_config(15, 3));
    sim::SimulationRunner b(make_config(15, 4));
    while (!a.finished()) {
        a.step();
        b.step();
    }

    CHECK(same(a.results(), sim::run_simulation(make_config(15, 3))));
    CHECK(same(b.results(), sim::run_simulation(make_config(15, 4))));
}

// ----------------------------------------------------------------- observers

TEST_CASE("An extra observer watches the same run", "[simulation][runner]") {
    sim::SimulationRunner runner(make_config(30));
    Probe probe(runner);
    runner.add_observer(probe);

    runner.run();

    const sim::AssetResults& portfolio = runner.results().portfolio;
    CHECK(probe.ticks_seen == 30);
    CHECK(probe.adverse_aggressor == portfolio.adverse_trade_count);
    CHECK(probe.mm_aggressor == portfolio.mm_trade_count);
    CHECK(portfolio.adverse_trade_count > 0);
}

TEST_CASE("The built-in observer sees a tick before any observer added by the caller",
          "[simulation][runner]") {
    sim::SimulationRunner runner(make_config(6));
    Probe probe(runner);
    runner.add_observer(probe);

    runner.run();

    // At tick n the results already contain n increments when the probe is called.
    REQUIRE(probe.results_ticks_seen.size() == 6);
    for (std::size_t i = 0; i < probe.results_ticks_seen.size(); ++i) {
        CHECK(probe.results_ticks_seen[i] == static_cast<std::int64_t>(i) + 1);
    }
}

TEST_CASE("An observer added after some ticks sees only the following ones",
          "[simulation][runner]") {
    sim::SimulationRunner runner(make_config(10));
    for (int tick = 0; tick < 4; ++tick) {
        runner.step();
    }
    Probe probe(runner);
    runner.add_observer(probe);

    runner.run();

    CHECK(probe.ticks_seen == 6);
}

TEST_CASE("A run under the default geometric dynamics completes with sane results",
          "[simulation][runner][dynamics]") {
    sim::SimulationConfig config = make_config(50);
    config.process = sim::RandomWalkSpec{};
    for (sim::AssetSpec& asset : config.assets) {
        asset.price_volatility = 0.01; // relative now: 1% per sqrt(second)
    }

    sim::SimulationRunner runner(config);
    const sim::Results& results = runner.run();

    CHECK(runner.finished());
    CHECK(results.assets.size() == 2);
    CHECK(std::isfinite(results.portfolio.final_breakdown.total()));
    CHECK(runner.world().price_process().prices().minCoeff() > 0.0);
}

TEST_CASE("A run with no agent has nothing to report on", "[simulation][runner]") {
    sim::SimulationConfig config = make_config(5);
    config.agents.clear();

    CHECK_THROWS_AS(sim::SimulationRunner(config), std::invalid_argument);
}

TEST_CASE("The results describe the first market maker, whatever comes before it",
          "[simulation][runner]") {
    sim::SimulationConfig config = make_config(20);
    config.agents.insert(config.agents.begin(),
                         sim::AgentSpec{.profile = {.name = "bystander", .initial_cash = 0.0},
                                        .timing = {},
                                        .behavior = mms::agents::InformedFlowSpec{
                                            .lambda = 1.0,
                                            .direction_correlation = 0.0,
                                            .size = sim::UniformIntSpec{.min = 1, .max = 2}}});

    sim::SimulationRunner runner(config);
    runner.run();

    REQUIRE(runner.world().agent_count() == 2);
    CHECK(runner.results().portfolio.final_breakdown.total() ==
          runner.world().agent(1).account().ledger().portfolio().total());
    CHECK(runner.results().portfolio.final_breakdown.total() != 0.0);
}
