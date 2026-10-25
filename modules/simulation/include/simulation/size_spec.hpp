#ifndef MMS_SIMULATION_SIZE_SPEC_HPP
#define MMS_SIMULATION_SIZE_SPEC_HPP

/// \file
/// The size specs live in MMS::agents now, where the agents that draw sizes are
/// (agents/size_spec.hpp). These names are kept here for the code that still uses them
/// through MMS::simulation.

#include <agents/size_spec.hpp>

namespace mms::simulation {

using agents::DistributionSpec;
using agents::ExponentialSpec;
using agents::UniformIntSpec;

} // namespace mms::simulation

#endif // MMS_SIMULATION_SIZE_SPEC_HPP
