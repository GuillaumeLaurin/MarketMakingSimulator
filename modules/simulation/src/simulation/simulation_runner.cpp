/// \file
/// Implementation of SimulationRunner (<simulation/simulation_runner.hpp>).

#include <cstdint>
#include <stdexcept>

#include <agents/market_maker.hpp>

#include <simulation/simulation_runner.hpp>

#include "simulation/results.hpp"

namespace mms::simulation {

namespace {

// Runs first in the initializer list, so a negative count is rejected before
// the World, which is the expensive part, is built.
std::int64_t validated_tick_count(const SimulationConfig& config) {
    if (config.tick_count < 0) {
        throw std::invalid_argument("SimulationRunner: tick_count must not be negative");
    }
    return config.tick_count;
}

// The results still describe one agent: the first market maker, or failing that the first
// agent. Per-agent results come later.
const mm::PnlLedger& reported_ledger(const World& world) {
    for (AgentId id = 0; id < world.agent_count(); ++id) {
        if (dynamic_cast<const agents::MarketMakerAgent*>(&world.agent(id).agent()) != nullptr) {
            return world.agent(id).account().ledger();
        }
    }
    if (world.agent_count() == 0) {
        throw std::invalid_argument("SimulationRunner: there is no agent to report on");
    }
    return world.agent(0).account().ledger();
}

} // namespace

SimulationRunner::SimulationRunner(const SimulationConfig& config)
    : tick_count_(validated_tick_count(config)), world_(config), loop_(world_, config),
      observer_(reported_ledger(world_)) {
    // Registered here, before any caller can add its own: the built-in
    // observer always sees a tick first.
    loop_.add_observer(observer_);
}

void SimulationRunner::add_observer(SimulationObserver& observer) {
    loop_.add_observer(observer);
}

void SimulationRunner::step() {
    if (finished()) {
        throw std::logic_error("SimulationRunner::step: the run is already finished");
    }
    loop_.step();
}

const Results& SimulationRunner::run() {
    while (!finished()) {
        loop_.step();
    }
    return observer_.results();
}

Results run_simulation(const SimulationConfig& config) {
    SimulationRunner runner(config);
    return runner.run();
}

} // namespace mms::simulation