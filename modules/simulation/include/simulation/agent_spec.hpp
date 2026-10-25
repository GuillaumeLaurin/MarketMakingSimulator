#ifndef MMS_SIMULATION_AGENT_SPEC_HPP
#define MMS_SIMULATION_AGENT_SPEC_HPP

/// \file
/// One agent of a simulation, as data: who it is, how fast it lives, and what it does.

#include <agents/agent_spec.hpp>

#include <simulation/agent_profile.hpp>
#include <simulation/agent_timing.hpp>

namespace mms::simulation {

/// The agents act in the order they are listed in SimulationConfig::agents, and messages
/// due at the same time reach the exchange in that order too.
struct AgentSpec {
    AgentProfile profile;
    AgentTiming timing;
    agents::AgentBehavior behavior;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_AGENT_SPEC_HPP
