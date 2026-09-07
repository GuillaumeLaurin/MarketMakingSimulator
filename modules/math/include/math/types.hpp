#ifndef MMS_MATH_TYPES_HPP
#define MMS_MATH_TYPES_HPP

/// \file
/// The linear-algebra vocabulary types for MMS::math. Every zone that touches
/// vectors or matrices (covariance, price processes, Avellaneda-Stoikov) works
/// in these aliases; this is also the single include point for Eigen inside the
/// module, so any EIGEN_* configuration lives in one place.

#include <Eigen/Core>

namespace mms::math {

/// Dynamic-size column vector of double.
using Vector = Eigen::VectorXd;

/// Dynamic-size dense matrix of double.
using Matrix = Eigen::MatrixXd;

/// Eigen's signed index type; use it for sizes and loop counters over Vector /
/// Matrix to stay clear of signed/unsigned mismatches with Eigen's API.
using Index = Eigen::Index;

} // namespace mms::math

#endif // MMS_MATH_TYPES_HPP