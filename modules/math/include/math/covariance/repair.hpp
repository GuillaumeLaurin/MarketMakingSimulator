#ifndef MMS_MATH_COVARIANCE_REPAIR_HPP
#define MMS_MATH_COVARIANCE_REPAIR_HPP

/// \file
/// Symmetric-matrix repair primitives for CovarianceMatrix. clip_eigenvalues()
/// is a spectral projection: it raises every eigenvalue to at least epsilon.
/// nearest_correlation_higham() is Higham's (2002) alternating-projections
/// iteration for the nearest correlation matrix. Free functions, isolated so
/// they can be tested without the CovarianceMatrix machinery. Both use an
/// iterative eigensolver: deterministic per platform, tolerance-level across
/// platforms (Target A); the repair path only runs on invalid config input.

#include <cstdint>

#include <math/math_export.hpp>
#include <math/types.hpp>

namespace mms::math::covariance {

/// Whether a factory may modify its input to establish the positive-definite
/// invariant. Repair is never silent: it fills a RepairReport.
enum class RepairPolicy : std::uint8_t { None, Repair };

/// What a repair did.
struct RepairReport {
    double frobenius_distance = 0.0;
    double min_eigenvalue_before = 0.0;
    double min_eigenvalue_after = 0.0;
    double max_abs_entry_change = 0.0;
    int iterations = 0;
};

/// A repaired matrix plus the report describing the change.
struct RepairResult {
    Matrix matrix;
    RepairReport report;
};

/// Project a symmetric matrix onto {eigenvalues >= epsilon} via spectral
/// decomposition, then re-symmetrise. epsilon == 0 is the nearest PSD matrix;
/// epsilon > 0 is strictly positive definite. Narrow contract: `a` square,
/// symmetric, finite.
[[nodiscard]] MMS_MATH_EXPORT RepairResult clip_eigenvalues(const Matrix& a, double epsilon);

/// Nearest correlation matrix to `a` by Higham's alternating projections:
/// alternate projecting onto the PSD cone (eigenvalues raised to
/// min_eigenvalue) and onto the unit-diagonal set, with Dykstra's correction.
/// min_eigenvalue == 0 is the classic result (PSD, possibly singular);
/// min_eigenvalue > 0 forces a strictly PD result. Narrow contract: `a` square,
/// symmetric, finite, roughly unit diagonal.
[[nodiscard]] MMS_MATH_EXPORT RepairResult nearest_correlation_higham(const Matrix& a,
                                                                      double min_eigenvalue = 0.0,
                                                                      double tol = 1e-10,
                                                                      int max_iterations = 100);

} // namespace mms::math::covariance

#endif // MMS_MATH_COVARIANCE_REPAIR_HPP