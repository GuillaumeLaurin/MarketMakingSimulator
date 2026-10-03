#ifndef MMS_SIMULATION_LATENCY_HPP
#define MMS_SIMULATION_LATENCY_HPP

/// \file
/// Turning a DelaySpec into an actual delay: how long one message (a quote
/// submit, a quote cancel, or an adverse-flow order) takes to reach the
/// exchange. Delays are in the simulation's own time unit, the same one as
/// dt, every rate and mm::HorizonPolicy's tau, so a drawn delay adds
/// straight onto the simulation clock to give the time a request is due.
/// The tick loop draws from World::latency_engine(asset).

#include <math/rng/engine.hpp>
#include <simulation/delay_spec.hpp>
#include <simulation/simulation_export.hpp>

namespace mms::simulation {

/// Checks that `spec` describes a valid delay: ConstantDelaySpec needs a
/// finite delay >= 0, UniformDelaySpec a finite 0 <= min <= max,
/// ExponentialDelaySpec a finite mean > 0. Public so that whatever builds
/// a run can reject a bad LatencyConfig up front, instead of failing at
/// some tick deep into it. Throws std::invalid_argument.
MMS_SIMULATION_EXPORT void validate_delay_spec(const DelaySpec& spec);

/// One delay drawn from `spec`, never negative and never infinite.
///   ConstantDelaySpec    -> exactly `delay`; consumes NO engine word.
///   UniformDelaySpec     -> uniform over [min, max); `max` itself has
///                           probability 0; min == max is allowed; 1 word.
///   ExponentialDelaySpec -> exponential with the given mean, clamped to
///                           the largest finite double; 1 word.
/// Validates first (see validate_delay_spec); a rejected call consumes
/// nothing.
[[nodiscard]] MMS_SIMULATION_EXPORT double draw_delay(const DelaySpec& spec,
                                                      math::rng::RngEngine& engine);

} // namespace mms::simulation

#endif // MMS_SIMULATION_LATENCY_HPP