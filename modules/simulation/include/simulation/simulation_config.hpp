#ifndef MMS_SIMULATION_SIMULATION_CONFIG_HPP
#define MMS_SIMULATION_SIMULATION_CONFIG_HPP

/// \file
/// Everything one run of the simulation needs. Plain data, no invariant
/// enforced here: World validates sizes and values against each other when
/// it actually builds the PriceProcess, the CovarianceMatrix and the
/// strategy, the same way every spec and config in this zone defers
/// validation to wherever the context to check it against exists.

#include <cstdint>
#include <vector>

#include <math/rng/seed.hpp>
#include <math/types.hpp>
#include <mm/strategy_config.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/latency_config.hpp>
#include <simulation/price_process_spec.hpp>

namespace mms::simulation {

struct SimulationConfig {
    /// One entry per asset, in AssetId order. Its size is the asset count
    /// every other per-asset vector (correlation's dimension, strategy's own
    /// per-asset fields) must match.
    std::vector<AssetSpec> assets;

    /// N x N correlation of the true prices; symmetric, positive definite.
    math::Matrix correlation;

    /// Which price-process mechanism drives the whole basket
    PriceProcessSpec process;

    /// The market maker's own quoting policy. Stays in its own
    /// parallel-vector form (mm's convention); World assembles its strategy
    /// from this and feeds it the live market data each tick.
    mm::StrategyConfig strategy;

    LatencyConfig latency;

    /// Expanded into every RngHub stream this run needs (price, adverse
    /// flow, latency), per asset, by World.
    math::rng::Seed master_seed;

    /// The simulation's fixed time step, in its own time unit (the same one
    /// as every rate and delay in this zone, and as mm::HorizonPolicy's tau).
    double dt;

    /// How many ticks the run advances for.
    std::int64_t tick_count;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_SIMULATION_CONFIG_HPP