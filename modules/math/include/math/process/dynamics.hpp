#ifndef MMS_MATH_PROCESS_DYNAMICS_HPP
#define MMS_MATH_PROCESS_DYNAMICS_HPP

/// \file
/// How a price process turns its random increment into a new price.

#include <cstdint>

namespace mms::math::process {

enum class Dynamics : std::uint8_t {
    /// p += mu * dt + increment. mu and the volatilities are in price units,
    /// and a price can cross zero.
    Arithmetic,
    /// p *= exp((mu - variance / 2) * dt + increment). mu is the expected
    /// return rate, the volatilities are relative, and a positive price stays
    /// positive. variance is the per-unit-time variance of the asset's log
    /// return, so E[p(t)] = p(0) * exp(mu * t).
    Geometric,
};

} // namespace mms::math::process

#endif // MMS_MATH_PROCESS_DYNAMICS_HPP
