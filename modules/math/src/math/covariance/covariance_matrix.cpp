/// \file
/// Implementation of CovarianceMatrix (<math/covariance/covariance_matrix.hpp>).

#include <cmath>
#include <stdexcept>
#include <string>

#include <math/covariance/covariance_matrix.hpp>

#include <Eigen/Cholesky>
#include <Eigen/Eigenvalues>

namespace mms::math::covariance {

namespace {

/// Squared-pivot floor for "numerically positive definite", relative to the
/// average diagonal (trace / n).
constexpr double relative_pd_floor = 1e-12;

/// A repair targets a floor this many times above relative_pd_floor. The
/// acceptance test (numerically_pd) measures the smallest Cholesky pivot
/// squared, while a repair (clip_eigenvalues / nearest_correlation_higham)
/// targets a floor on the true eigenvalues - related but not numerically
/// identical quantities. Without a safety margin a freshly repaired matrix can
/// land squarely on the acceptance boundary and fail it by rounding alone.
constexpr double repair_safety_factor = 100.0;

Matrix symmetrised(const Matrix& a) {
    return ((a + a.transpose()) * 0.5).eval();
}

/// LLT succeeds and the smallest pivot L_ii^2 exceeds relative_pd_floor *
/// trace / n.
bool numerically_pd(const Matrix& a) {
    const Eigen::LLT<Matrix> llt(a);
    if (llt.info() != Eigen::Success) {
        return false;
    }
    const double min_diag = llt.matrixLLT().diagonal().minCoeff();
    const double avg_diag = a.trace() / static_cast<double>(a.rows());
    return (min_diag * min_diag) > (relative_pd_floor * avg_diag);
}

Matrix lower_cholesky(const Matrix& a) {
    return Eigen::LLT<Matrix>(a).matrixL().toDenseMatrix(); // dense L, zeros above
}

[[noreturn]] void reject(const Matrix& a) {
    const double lambda_min =
        Eigen::SelfAdjointEigenSolver<Matrix>(a, Eigen::EigenvaluesOnly).eigenvalues().minCoeff();
    throw std::invalid_argument(
        "CovarianceMatrix: input is not positive definite (min_eigenvalue " +
        std::to_string(lambda_min) + "); pass RepairPolicy::Repair or use a factor model");
}

} // namespace

CovarianceMatrix CovarianceMatrix::from_matrix(const Matrix& sigma, RepairPolicy policy) {
    Matrix s = symmetrised(sigma);
    std::optional<RepairReport> report;

    if (!numerically_pd(s)) {
        if (policy == RepairPolicy::None) {
            reject(s);
        }
        const double floor =
            repair_safety_factor * relative_pd_floor * (s.trace() / static_cast<double>(s.rows()));
        RepairResult repaired = clip_eigenvalues(s, floor);
        s = std::move(repaired.matrix);
        report = repaired.report;
        if (!numerically_pd(s)) {
            reject(s); // unreachable after clipping to a positive floor
        }
    }
    return {s, lower_cholesky(s), report};
}

CovarianceMatrix CovarianceMatrix::from_correlation(std::span<const double> vols,
                                                    const Matrix& corr,
                                                    RepairPolicy policy) {
    const auto n = static_cast<Index>(vols.size());
    if (corr.rows() != n || corr.cols() != n) {
        throw std::invalid_argument(
            "CovarianceMatrix::from_correlation: corr must be a vols.size() square matrix");
    }
    for (const double v : vols) {
        if (!std::isfinite(v) || v <= 0.0) {
            throw std::invalid_argument(
                "CovarianceMatrix::from_correlation: volatilities must be finite and > 0");
        }
    }

    Matrix r = symmetrised(corr);
    r.diagonal().setOnes();

    std::optional<RepairReport> report;
    if (!numerically_pd(r)) {
        if (policy == RepairPolicy::None) {
            reject(r);
        }
        RepairResult repaired =
            nearest_correlation_higham(r, repair_safety_factor * relative_pd_floor);
        r = std::move(repaired.matrix);
        report = repaired.report;
        if (!numerically_pd(r)) {
            reject(r);
        }
    }

    Matrix d = Matrix::Zero(n, n);
    for (Index i = 0; i < n; ++i) {
        d(i, i) = vols[static_cast<std::size_t>(i)];
    }
    const Matrix sigma = symmetrised(d * r * d);

    return {sigma, lower_cholesky(sigma), report};
}

Matrix CovarianceMatrix::factor(std::span<const std::size_t> active) const {
    const auto n = static_cast<std::size_t>(sigma_.rows());
    for (const std::size_t idx : active) {
        if (idx >= n) {
            throw std::out_of_range("CovarianceMatrix::factor: active index out of range");
        }
    }

    if (active.size() == n) {
        bool identity_order = true;
        for (std::size_t i = 0; i < n; ++i) {
            if (active[i] != i) {
                identity_order = false;
                break;
            }
        }
        if (identity_order) {
            return chol_;
        }
    }

    const auto k = static_cast<Index>(active.size());
    Matrix sub(k, k);
    for (Index i = 0; i < k; ++i) {
        for (Index j = 0; j < k; ++j) {
            sub(i, j) = sigma_(static_cast<Index>(active[static_cast<std::size_t>(i)]),
                               static_cast<Index>(active[static_cast<std::size_t>(j)]));
        }
    }
    return lower_cholesky(sub);
}

} // namespace mms::math::covariance