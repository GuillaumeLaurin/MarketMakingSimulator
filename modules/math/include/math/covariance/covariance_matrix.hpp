#ifndef MMS_MATH_COVARIANCE_COVARIANCE_MATRIX_HPP
#define MMS_MATH_COVARIANCE_COVARIANCE_MATRIX_HPP

/// \file
/// CovarianceMatrix: the validated, factorised universe covariance. Built once
/// from config via from_matrix / from_correlation, then queried per run for the
/// Cholesky factor of the active asset subset. Strict positive-definite
/// invariant (unpivoted Eigen::LLT); a rank-deficient or near-singular input is
/// rejected unless RepairPolicy::Repair is passed, which repairs it and records
/// a RepairReport. Immutable value type: the factor is computed once and stored.

#include <cstddef>
#include <optional>
#include <span>
#include <utility>

#include <math/covariance/repair.hpp>
#include <math/math_export.hpp>
#include <math/types.hpp>

namespace mms::math::covariance {

class MMS_MATH_EXPORT CovarianceMatrix {
public:
    /// Build from a covariance matrix, symmetrised (A + A^T) / 2 first. Throws
    /// std::invalid_argument if the result is not positive definite and policy
    /// is RepairPolicy::None.
    [[nodiscard]] static CovarianceMatrix from_matrix(const Matrix& sigma,
                                                      RepairPolicy policy = RepairPolicy::None);

    /// Build from per-asset volatilities and a correlation matrix:
    /// Sigma = D R D with D = diag(vols). `vols` must be finite and > 0; `corr`
    /// is symmetrised with its diagonal forced to 1. Throws std::invalid_argument
    /// on a bad input that policy does not allow repairing. When a repair
    /// happens it is applied to R, so the RepairReport is in correlation units.
    [[nodiscard]] static CovarianceMatrix from_correlation(
        std::span<const double> vols, const Matrix& corr, RepairPolicy policy = RepairPolicy::None);

    [[nodiscard]] Index dimension() const noexcept { return sigma_.rows(); }

    /// The validated, symmetrised covariance.
    [[nodiscard]] const Matrix& matrix() const noexcept { return sigma_; }

    /// Lower-triangular Cholesky factor L (Sigma == L L^T), zeros above.
    [[nodiscard]] const Matrix& cholesky() const noexcept { return chol_; }

    /// Cholesky factor of the principal submatrix indexed by `active`. `active`
    /// must be sorted ascending and duplicate-free (narrow contract); an index
    /// >= dimension() throws std::out_of_range. Returns the cached full factor
    /// when `active` covers every index in order. Cannot fail: a principal
    /// submatrix of a positive-definite matrix is positive definite.
    [[nodiscard]] Matrix factor(std::span<const std::size_t> active) const;

    [[nodiscard]] const std::optional<RepairReport>& repair_report() const noexcept {
        return report_;
    }

private:
    CovarianceMatrix(Matrix sigma, Matrix chol, std::optional<RepairReport> report)
        : sigma_(std::move(sigma)), chol_(std::move(chol)), report_(report) {}

    Matrix sigma_;
    Matrix chol_;
    std::optional<RepairReport> report_;
};

} // namespace mms::math::covariance

#endif // MMS_MATH_COVARIANCE_COVARIANCE_MATRIX_HPP