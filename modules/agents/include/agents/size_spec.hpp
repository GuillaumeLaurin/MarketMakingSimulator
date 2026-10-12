#ifndef MMS_AGENTS_SIZE_SPEC_HPP
#define MMS_AGENTS_SIZE_SPEC_HPP

/// \file
/// The size of one trade an agent sends: a small, extensible set of distributions. Kept
/// apart from the delay specs of MMS::simulation because a size is a whole number of
/// shares and a delay is a duration; sharing one generic variant across both would buy
/// little and would couple two things that may want to diverge.

#include <cstdint>
#include <variant>

#include <mm/types.hpp>

namespace mms::agents {

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

/// What kind of size a trade gets. Converted to an actual draw by the free
/// function `draw_size` (agents/flow.hpp).
using DistributionSpec = std::variant<UniformIntSpec, ExponentialSpec>;

} // namespace mms::agents

#endif // MMS_AGENTS_SIZE_SPEC_HPP
