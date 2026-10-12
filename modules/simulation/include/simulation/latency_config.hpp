#ifndef MMS_SIMULATION_LATENCY_CONFIG_HPP
#define MMS_SIMULATION_LATENCY_CONFIG_HPP

/// \file
/// How long adverse flow takes to reach the exchange. It is often faster than the market
/// maker (that speed is exactly what lets it beat the market maker to a stale quote). The
/// delays of the agents are in their own AgentTiming.

#include <simulation/delay_spec.hpp>

namespace mms::simulation {

struct LatencyConfig {
    DelaySpec adverse_flow_delay = ConstantDelaySpec{.delay = 0.0};
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_LATENCY_CONFIG_HPP
