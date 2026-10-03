#ifndef MMS_SIMULATION_SIZE_SPEC_HPP
#define MMS_SIMULATION_SIZE_SPEC_HPP

/// \file
/// The trade size of one adverse-flow occurrence: a small, extensible set of
/// distributions, kept separate from DelaySpec (delay_spec.hpp) because a
/// size is a whole number of shares and a delay is a duration - sharing one
/// generic variant across both would buy little and would couple two things
/// that may want to diverge.

#include <cstdint>
#include <variant>

#include <mm/types.hpp>

namespace mms::simulation {

/// A whole number of shares, uniform over [min, max] inclusive.
struct UniformIntSpec {
    mm::Quantity min;
    mm::Quantity max;
};

/// Drawn from an exponential distribution of the given mean, then rounded to
/// the nearest whole share and floored at 1 (a trade of size 0 is not a
/// trade).
struct ExponentialSpec {
    double mean;
};

/// What kind of size an occurrence of adverse flow gets. Converted to an
/// actual draw by the free function `draw_size`
using DistributionSpec = std::variant<UniformIntSpec, ExponentialSpec>;

} // namespace mms::simulation

#endif // MMS_SIMULATION_SIZE_SPEC_HPP