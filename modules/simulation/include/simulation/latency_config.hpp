#ifndef MMS_SIMULATION_LATENCY_CONFIG_HPP
#define MMS_SIMULATION_LATENCY_CONFIG_HPP

/// \file
/// How long messages take to reach the exchange. Two separate delay
/// distributions: one for the market maker's own quote submits and cancels,
/// one for adverse flow, which is often faster (that speed is exactly what
/// lets it beat the market maker to a stale quote).

#include <simulation/delay_spec.hpp>

namespace mms::simulation {

struct LatencyConfig {
    DelaySpec quote_delay;
    DelaySpec adverse_flow_delay;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_LATENCY_CONFIG_HPP