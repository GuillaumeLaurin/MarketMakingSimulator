#ifndef MMS_MATH_RNG_ENGINE_HPP
#define MMS_MATH_RNG_ENGINE_HPP

/// \file
/// RngEngine: the project's concrete bit generator, Philox-4x64-10 driven by
/// CounterBasedEngine. Change the policy / round count here to change it
/// project-wide.

#include <math/rng/counter_based_engine.hpp>
#include <math/rng/philox.hpp>

namespace mms::math::rng {

using RngEngine = CounterBasedEngine<PhiloxBlock, 10>;

} // namespace mms::math::rng

#endif // MMS_MATH_RNG_ENGINE_HPP