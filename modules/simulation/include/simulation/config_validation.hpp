#ifndef MMS_SIMULATION_CONFIG_VALIDATION_HPP
#define MMS_SIMULATION_CONFIG_VALIDATION_HPP

/// \file
/// The checks on the part of a SimulationConfig that describes the agents and the
/// exchange they trade on, made in one place and before the first random draw.

#include <simulation/simulation_config.hpp>
#include <simulation/simulation_export.hpp>

namespace mms::simulation {

/// Throws std::invalid_argument for:
///   an asset whose tick_size is not finite and > 0
///   a book_depth below 1
///   account_rules that are not finite and >= 0
///   an agent without a name, or two agents sharing a name
///   an agent whose decision_period is not finite and > 0, or whose order_delay or
///   info_delay is not a valid delay (see validate_delay_spec)
/// What an agent's own profile and behavior must satisfy is checked where they are used
/// (Account, make_agent).
MMS_SIMULATION_EXPORT void validate_agent_setup(const SimulationConfig& config);

} // namespace mms::simulation

#endif // MMS_SIMULATION_CONFIG_VALIDATION_HPP
