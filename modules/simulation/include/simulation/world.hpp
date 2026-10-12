#ifndef MMS_SIMULATION_WORLD_HPP
#define MMS_SIMULATION_WORLD_HPP

/// \file
/// World: everything a run advances, built once from a SimulationConfig and
/// then driven tick by tick by tick_loop.cpp.
/// ONE math::process::PriceProcess for the whole correlated basket (never
/// one per asset: a single process object advances every asset's true price
/// jointly), one mm::MatchingEngine per asset, one event queue for everything on its way to
/// an exchange, and one AgentRuntime per agent. Builds the CovarianceMatrix once at
/// construction and uses it to derive whatever the chosen PriceProcess needs.

#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

#include <mm/types.hpp>

#include <core/latency_queue.hpp>
#include <math/process/price_process.hpp>
#include <math/rng/engine.hpp>
#include <simulation/agent_id.hpp>
#include <simulation/agent_runtime.hpp>
#include <simulation/simulation_config.hpp>
#include <simulation/simulation_export.hpp>

namespace mms::simulation {

/// Everything that can be in flight: the messages of the agents. One shared queue gives a
/// single, time-ordered view of all of it (SIM27).
using SimulationEvent = std::variant<SubmitArrival, CancelArrival>;

using EventQueue = core::LatencyQueue<SimulationEvent>;

class MMS_SIMULATION_EXPORT World {
public:
    /// Builds everything from `config`: the CovarianceMatrix, the one
    /// PriceProcess, one MatchingEngine per asset, one AgentRuntime per agent
    /// (each with its streams). Validates the agent setup first (validate_agent_setup). Throws
    /// std::invalid_argument for anything inconsistent: `config.assets` empty or its size
    /// disagreeing with `correlation`'s dimension, or a market maker whose strategy has another
    /// number of assets; an asset without a symbol, or two assets with the same symbol; a
    /// FactorModelSpec process with an asset missing `factor_model`, or a
    /// RandomWalkSpec process with one present; a Geometric process with an
    /// initial_mid that is not finite and positive; a correlation that is not
    /// symmetric positive definite; anything an agent.s, an account.s or the
    /// PriceProcess.s own constructor rejects.
    explicit World(const SimulationConfig& config);

    [[nodiscard]] std::size_t asset_count() const noexcept { return engines_.size(); }

    [[nodiscard]] math::process::PriceProcess& price_process() noexcept { return *price_process_; }

    [[nodiscard]] const math::process::PriceProcess& price_process() const noexcept {
        return *price_process_;
    }

    [[nodiscard]] mm::MatchingEngine& matching_engine(mm::AssetId asset) {
        return engines_.at(asset);
    }

    [[nodiscard]] const mm::MatchingEngine& matching_engine(mm::AssetId asset) const {
        return engines_.at(asset);
    }

    [[nodiscard]] std::size_t agent_count() const noexcept { return agents_.size(); }

    /// The agent at this position of config.agents, with its account and its orders.
    [[nodiscard]] AgentRuntime& agent(AgentId id) { return agents_.at(id); }

    [[nodiscard]] const AgentRuntime& agent(AgentId id) const { return agents_.at(id); }

    /// The one queue of everything still on its way to an exchange: the
    /// messages of the agents,
    /// all assets together, released in (due time, insertion order) order.
    [[nodiscard]] EventQueue& event_queue() noexcept { return event_queue_; }

private:
    std::unique_ptr<math::process::PriceProcess> price_process_;
    std::vector<mm::MatchingEngine> engines_;
    EventQueue event_queue_;
    std::vector<AgentRuntime> agents_;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_WORLD_HPP