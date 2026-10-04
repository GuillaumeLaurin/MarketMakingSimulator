/// \file
/// The <simulation/simulation.hpp> umbrella pulls in every public symbol of
/// the simulation module: one small scenario touching each header, from the
/// config specs through the adverse-flow and latency draws to a complete run
/// observed by two observers.

#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <simulation/simulation.hpp>

namespace {

struct TickCounter final : mms::simulation::SimulationObserver {
    std::int64_t ticks = 0;

    void on_tick_end(double /*time*/) override { ++ticks; }
};

} // namespace

TEST_CASE("<simulation/simulation.hpp> exposes the whole simulation module",
          "[simulation][umbrella]") {
    using namespace mms::simulation;
    namespace math = mms::math;
    namespace mm = mms::mm;

    // size_spec.hpp, delay_spec.hpp, price_process_spec.hpp, asset_spec.hpp,
    // latency_config.hpp, simulation_config.hpp: the whole config.
    const AssetSpec asset{.symbol = "AAPL",
                          .initial_mid = 100.0,
                          .maker_fee_bps = 1.0,
                          .price_volatility = 1.0,
                          .mu = 0.0,
                          .factor_model = std::nullopt,
                          .adverse_flow_lambda = 3.0,
                          .adverse_flow_direction_correlation = 0.5,
                          .adverse_flow_size = UniformIntSpec{.min = 1, .max = 5}};
    const SimulationConfig config{
        .assets = {asset},
        .correlation = math::Matrix::Identity(1, 1),
        .process = RandomWalkSpec{},
        .strategy = mm::StrategyConfig{.gamma = 2.0,
                                       .k = math::Vector::Constant(1, 1.0),
                                       .correlation = math::Matrix::Identity(1, 1),
                                       .estimators = {mm::ConstantSpec{.sigma = 0.2}},
                                       .horizon = mm::ConstantHorizon{.tau = 1.0},
                                       .quote_sizes = {10},
                                       .tick_sizes = {0.01},
                                       .limits = {.max_abs_position = {1000},
                                                  .max_portfolio_risk = std::nullopt},
                                       .min_samples = 0},
        .latency = {.quote_delay = ConstantDelaySpec{.delay = 0.0},
                    .adverse_flow_delay = ConstantDelaySpec{.delay = 0.0}},
        .requote_threshold_ticks = 1,
        .master_seed = math::rng::Seed{5},
        .dt = 1.0,
        .tick_count = 20};

    // world.hpp: the pieces, and the per-asset random streams the draws below use.
    World world(config);
    REQUIRE(world.asset_count() == 1);

    // adverse_flow.hpp: orders for one tick, each of a size the spec allows.
    for (const mm::MarketOrder& order :
         generate_adverse_flow(asset, 1.0, config.dt, world.adverse_flow_engine(0))) {
        CHECK(order.quantity >= 1);
        CHECK(order.quantity <= 5);
    }

    // latency.hpp: a constant delay is validated and drawn as itself.
    const DelaySpec delay = config.latency.quote_delay;
    validate_delay_spec(delay);
    CHECK(draw_delay(delay, world.latency_engine(0)) == 0.0);

    // simulation_runner.hpp, tick_loop.hpp, results_observer.hpp,
    // simulation_observer.hpp, results.hpp: a run watched by a second observer.
    SimulationRunner runner(config);
    TickCounter counter;
    runner.add_observer(counter);

    const Results& results = runner.run();

    CHECK(runner.finished());
    CHECK(counter.ticks == 20);
    CHECK(results.assets.size() == 1);
    CHECK(results.portfolio.pnl_increments.count() == 20);

    // run_simulation: the one-call form is the same run.
    const Results again = run_simulation(config);
    CHECK(again.portfolio.final_breakdown.total() == results.portfolio.final_breakdown.total());
}
