/// \file
/// Implementation of SimulationRunner (<simulation/simulation_runner.hpp>).

#include <cstdint>
#include <stdexcept>

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

} // namespace

SimulationRunner::SimulationRunner(const SimulationConfig& config)
    : tick_count_(validated_tick_count(config)), world_(config), loop_(world_, config),
      observer_(observed_agents(world_)) {
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