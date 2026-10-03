#ifndef MMS_SIMULATION_DELAY_SPEC_HPP
#define MMS_SIMULATION_DELAY_SPEC_HPP

/// \file
/// How long a submit, cancel, or adverse-flow order takes to reach the
/// exchange: a small, extensible set of distributions over a delay in the
/// simulation's own time unit (the same one as dt and tau). Kept separate
/// from size_spec.hpp's DistributionSpec: a delay is a duration, not a whole
/// number of shares, and the two variants living in the same namespace would
/// collide if they shared any alternative's name.

#include <variant>

namespace mms::simulation {

/// Always the same delay, including 0 (adverse flow that never waits).
struct ConstantDelaySpec {
    double delay;
};

/// Uniform over [min, max] inclusive.
struct UniformDelaySpec {
    double min;
    double max;
};

/// Exponentially distributed with the given mean. The memoryless choice: the
/// delay still ahead does not depend on how long a message has already been
/// in flight.
struct ExponentialDelaySpec {
    double mean;
};

/// What kind of delay a submit, cancel, or adverse-flow order gets. Converted
/// to an actual draw wherever a RngEngine is available (latency.cpp)
using DelaySpec = std::variant<ConstantDelaySpec, UniformDelaySpec, ExponentialDelaySpec>;

} // namespace mms::simulation

#endif // MMS_SIMULATION_DELAY_SPEC_HPP