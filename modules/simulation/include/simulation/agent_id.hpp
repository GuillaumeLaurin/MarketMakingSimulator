#ifndef MMS_SIMULATION_AGENT_ID_HPP
#define MMS_SIMULATION_AGENT_ID_HPP

/// \file
/// AgentId: the position of an agent in the simulation's agent list. Like mm::AssetId it
/// is a positional index and not an identity: the identity of an agent is its name, which
/// its random streams are derived from.

#include <cstddef>

namespace mms::simulation {

using AgentId = std::size_t;

} // namespace mms::simulation

#endif // MMS_SIMULATION_AGENT_ID_HPP
