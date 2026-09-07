#ifndef MMS_MATH_STATS_TRAITS_HPP
#define MMS_MATH_STATS_TRAITS_HPP

/// \file
/// The VolatilityEstimator concept: the compile-time contract every scalar
/// per-asset volatility estimator models. Mirrors dist/traits.hpp (Sampler).
/// Note the naming asymmetry with the accumulators: an estimator uses update(),
/// the accumulators use add(). This concept is fed by mms, so it is its own
/// contract.

#include <concepts>
#include <cstdint>

namespace mms::math::stats {

template<class E>
concept VolatilityEstimator = requires(E e, const E ce, double x) {
    e.update(x);
    { ce.sigma() } -> std::convertible_to<double>;
    { ce.sample_count() } -> std::convertible_to<std::int64_t>;
};

} // namespace mms::math::stats

#endif // MMS_MATH_STATS_TRAITS_HPP