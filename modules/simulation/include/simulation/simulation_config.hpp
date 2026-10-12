#ifndef MMS_SIMULATION_SIMULATION_CONFIG_HPP
#define MMS_SIMULATION_SIMULATION_CONFIG_HPP

/// \file
/// Everything one run of the simulation needs. Plain data, no invariant
/// enforced here: World validates sizes and values against each other when
/// it actually builds the PriceProcess, the CovarianceMatrix and the
/// agents, the same way every spec and config in this zone defers
/// validation to wherever the context to check it against exists.

#include <cstdint>
#include <vector>

#include <math/rng/seed.hpp>
#include <math/types.hpp>
#include <simulation/account_rules.hpp>
#include <simulation/agent_spec.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/latency_config.hpp>
#include <simulation/price_process_spec.hpp>

namespace mms::simulation {

struct SimulationConfig {
    /// One entry per asset, in AssetId order. Its size is the asset count
    /// that the correlation matrix and a market maker strategy must match.
    std::vector<AssetSpec> assets;

    /// N x N correlation of the true prices; symmetric, positive definite.
    math::Matrix correlation;

    /// Which price-process mechanism drives the whole basket
    PriceProcessSpec process;

    LatencyConfig latency = {};

    /// The agents of the run, in the order they act.
    std::vector<AgentSpec> agents = {};

    /// The rules of the exchange that apply to every account alike.
    AccountRules account_rules = {};

    /// How many price levels of each side of a book an agent sees (AssetView). >= 1.
    int book_depth = 5;

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