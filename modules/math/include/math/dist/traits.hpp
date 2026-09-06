#ifndef MMS_MATH_DIST_TRAITS_HPP
#define MMS_MATH_DIST_TRAITS_HPP

/// \file
/// The Sampler concept: the compile-time contract every distribution models.

#include <concepts>
#include <type_traits>

#include <math/rng/engine.hpp> // RngEngine, the witness generator

namespace mms::math::dist {

/// An immutable value type that draws one variate per call.
///     - result type           : the variate type (bool, double, std::int64_t, ...)
///     - operator()(G&) const  : the draw, templated on any Uint64Source
///     - words_per_sample      : static engine-word cost; -1 means variable
/// RngEngine is used as the concrete witness, so modelling Sampler also proves
/// the sampler compiles against the production engine.
template <class S>
concept Sampler = std::is_arithmetic_v<typename S::result_type> &&
                  requires(const S& s, rng::RngEngine& g) {
                    { s(g) } -> std::same_as<typename S::result_type>;
                    { S::words_per_sample } -> std::convertible_to<int>;
                  };

} // namespace mms::math::dist

#endif // MMS_MATH_DIST_TRAITS_HPP