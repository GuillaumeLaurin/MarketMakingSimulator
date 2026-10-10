/// \file
/// Ticks per second of a whole SimulationRunner (price process, quoting
/// strategy, matching engines, adverse flow, ledger), by asset count. One
/// nanobench iteration is one tick, so ns/op is the cost of a tick and op/s is
/// the ticks per second. Run on demand: build/<preset>/out/bench/simulation_runner_ticks.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <mm/strategy_config.hpp>
#include <mm/types.hpp>
#include <nanobench.h>

#include <math/process/dynamics.hpp>
#include <math/rng/seed.hpp>
#include <math/types.hpp>
#include <simulation/simulation_runner.hpp>

namespace {

namespace sim = mms::simulation;
namespace math = mms::math;
namespace mm = mms::mm;

// More ticks than any benchmark will ever step, so the runner never finishes.
constexpr std::int64_t tick_budget = 1'000'000'000;

sim::SimulationConfig make_config(std::size_t asset_count) {
    std::vector<sim::AssetSpec> assets;
    assets.reserve(asset_count);
    for (std::size_t i = 0; i < asset_count; ++i) {
        assets.push_back(
            sim::AssetSpec{.symbol = "ASSET" + std::to_string(i),
                           .initial_mid = 100.0,
                           .maker_fee_bps = 1.0,
                           .price_volatility = 0.001,
                           .mu = 0.0,
                           .factor_model = std::nullopt,
                           .adverse_flow_lambda = 1.0,
                           .adverse_flow_direction_correlation = 0.5,
                           .adverse_flow_size = sim::UniformIntSpec{.min = 1, .max = 5}});
    }

    const auto n = static_cast<Eigen::Index>(asset_count);
    return sim::SimulationConfig{
        .assets = std::move(assets),
        .correlation = math::Matrix::Identity(n, n),
        .process = sim::RandomWalkSpec{.dynamics = math::process::Dynamics::Geometric},
        .strategy = mm::StrategyConfig{.gamma = 2.0,
                                       .k = math::Vector::Constant(n, 1.0),
                                       .correlation = math::Matrix::Identity(n, n),
                                       .estimators = std::vector<mm::EstimatorSpec>(
                                           asset_count, mm::ConstantSpec{.sigma = 0.2}),
                                       .horizon = mm::ConstantHorizon{.tau = 1.0},
                                       .quote_sizes = std::vector<mm::Quantity>(asset_count, 10),
                                       .tick_sizes = std::vector<mm::Price>(asset_count, 0.01),
                                       .limits = {.max_abs_position =
                                                      std::vector<mm::Quantity>(asset_count, 1000),
                                                  .max_portfolio_risk = std::nullopt},
                                       .min_samples = 0},
        .latency = {.quote_delay = sim::ConstantDelaySpec{.delay = 0.0},
                    .adverse_flow_delay = sim::ConstantDelaySpec{.delay = 0.0}},
        .requote_threshold_ticks = 1,
        .master_seed = math::rng::Seed{1},
        .dt = 1.0,
        .tick_count = tick_budget};
}

} // namespace

int main() {
    ankerl::nanobench::Bench bench;
    bench.title("simulation tick").unit("tick").warmup(200);

    for (const std::size_t asset_count : {1U, 10U, 100U}) {
        sim::SimulationRunner runner(make_config(asset_count));
        bench.run("runner, " + std::to_string(asset_count) + " assets", [&] { runner.step(); });
    }
    return 0;
}
