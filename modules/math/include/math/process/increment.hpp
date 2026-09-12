#ifndef MMS_MATH_PROCESS_INCREMENT_HPP
#define MMS_MATH_PROCESS_INCREMENT_HPP

/// \file
/// The shared "draw z, apply L" core used by every correlated price process:
/// one standard normal per engine, transformed through a lower-triangular
/// Cholesky factor and scaled by sqrt(dt). Returns only the stochastic term;
/// each process adds its own drift (mu * dt) separately, so this stays
/// reusable for any drift form (arithmetic random walk, log-space GBM, a
/// factor model's inner factor draw).

#include <span>

#include <math/math_export.hpp>
#include <math/rng/engine.hpp>
#include <math/types.hpp>

namespace mms::math::process {

/// sqrt(dt) * L * z, with z ~ N(0, I) drawn one word per engine (one engine
/// per row of chol). Narrow contract: chol.rows() == chol.cols() ==
/// engines.size(); chol is read only through its lower-triangular part.
[[nodiscard]] MMS_MATH_EXPORT Vector correlated_normal_increment(const Matrix& chol,
                                                                 std::span<rng::RngEngine> engines,
                                                                 double dt);

} // namespace mms::math::process

#endif // MMS_MATH_PROCESS_INCREMENT_HPP