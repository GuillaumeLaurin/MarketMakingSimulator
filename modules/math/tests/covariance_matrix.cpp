/// \file
/// Tests for CovarianceMatrix. Hand-computed SPD matrices pin from_matrix,
/// from_correlation's D R D reconstruction, factor()'s principal-submatrix
/// property (the Option-1 invariance), the None/Repair policies, and the
/// out-of-range contract on factor().

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <math/covariance/covariance_matrix.hpp>

namespace {

using mms::math::Matrix;
using mms::math::covariance::CovarianceMatrix;
using mms::math::covariance::RepairPolicy;

double max_abs_diff(const Matrix& a, const Matrix& b) {
    return (a - b).cwiseAbs().maxCoeff();
}

/// Reconstruct Sigma from a Cholesky factor: L L^T.
Matrix reconstructed(const Matrix& l) {
    return l * l.transpose();
}

} // namespace

TEST_CASE("CovarianceMatrix::from_matrix on a hand-computed SPD matrix", "[covariance][matrix]") {
    // Sigma = [[4, 2], [2, 3]] is SPD (eigenvalues 5.56.., 1.43..).
    // Its Cholesky factor: L = [[2, 0], [1, sqrt(2)]] since
    //   L L^T = [[4, 2], [2, 1 + 2]] = [[4, 2], [2, 3]].
    Matrix sigma(2, 2);
    sigma << 4.0, 2.0, 2.0, 3.0;

    const CovarianceMatrix cov = CovarianceMatrix::from_matrix(sigma);

    CHECK(cov.dimension() == 2);
    CHECK(max_abs_diff(cov.matrix(), sigma) < 1e-12);
    CHECK_FALSE(cov.repair_report().has_value());

    Matrix expected_l(2, 2);
    expected_l << 2.0, 0.0, 1.0, std::sqrt(2.0);
    CHECK(max_abs_diff(cov.cholesky(), expected_l) < 1e-9);
    CHECK(max_abs_diff(reconstructed(cov.cholesky()), sigma) < 1e-9);
}

TEST_CASE("CovarianceMatrix::from_matrix symmetrises an almost-symmetric input",
          "[covariance][matrix]") {
    Matrix sigma(2, 2);
    sigma << 4.0, 2.0000001, 1.9999999, 3.0; // tiny asymmetry from upstream rounding

    const CovarianceMatrix cov = CovarianceMatrix::from_matrix(sigma);
    CHECK(cov.matrix()(0, 1) == cov.matrix()(1, 0));
}

TEST_CASE("CovarianceMatrix::from_matrix rejects a non-positive-definite input by default",
          "[covariance][matrix]") {
    // eigenvalues {5, -1}: not PD.
    Matrix sigma(2, 2);
    sigma << 2.0, 3.0, 3.0, 2.0;

    CHECK_THROWS_AS(CovarianceMatrix::from_matrix(sigma), std::invalid_argument);
    CHECK_THROWS_WITH(CovarianceMatrix::from_matrix(sigma),
                      Catch::Matchers::ContainsSubstring("not positive definite"));
}

TEST_CASE("CovarianceMatrix::from_matrix repairs and reports when asked", "[covariance][matrix]") {
    Matrix sigma(2, 2);
    sigma << 2.0, 3.0, 3.0, 2.0;

    const CovarianceMatrix cov = CovarianceMatrix::from_matrix(sigma, RepairPolicy::Repair);

    REQUIRE(cov.repair_report().has_value());
    CHECK(cov.repair_report()->min_eigenvalue_before < 0.0);
    CHECK(cov.repair_report()->min_eigenvalue_after > 0.0);
    CHECK(cov.repair_report()->frobenius_distance > 0.0);

    // the repaired matrix must now be positive definite: L L^T reconstructs it.
    CHECK(max_abs_diff(reconstructed(cov.cholesky()), cov.matrix()) < 1e-9);
}

TEST_CASE("CovarianceMatrix::from_correlation reconstructs Sigma as D R D",
          "[covariance][matrix]") {
    const std::array<double, 3> vols{2.0, 3.0, 0.5};
    Matrix corr(3, 3);
    corr << 1.0, 0.3, 0.1, 0.3, 1.0, 0.2, 0.1, 0.2, 1.0;

    const CovarianceMatrix cov = CovarianceMatrix::from_correlation(vols, corr);

    Matrix expected(3, 3);
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            const auto ii = static_cast<mms::math::Index>(i);
            const auto jj = static_cast<mms::math::Index>(j);
            expected(ii, jj) = vols[i] * vols[j] * corr(ii, jj);
        }
    }
    CHECK(max_abs_diff(cov.matrix(), expected) < 1e-9);
    CHECK(max_abs_diff(reconstructed(cov.cholesky()), cov.matrix()) < 1e-9);
    // diagonal of Sigma is vols^2
    CHECK(cov.matrix()(0, 0) == Catch::Approx(4.0));
    CHECK(cov.matrix()(1, 1) == Catch::Approx(9.0));
    CHECK(cov.matrix()(2, 2) == Catch::Approx(0.25));
}

TEST_CASE("CovarianceMatrix::from_correlation validates its inputs", "[covariance][matrix]") {
    const std::array<double, 2> vols{1.0, 1.0};
    Matrix corr(2, 2);
    corr << 1.0, 0.5, 0.5, 1.0;

    const std::array<double, 2> bad_vols{1.0, -1.0};
    CHECK_THROWS_AS(CovarianceMatrix::from_correlation(bad_vols, corr), std::invalid_argument);

    Matrix wrong_size(3, 3);
    wrong_size.setIdentity();
    CHECK_THROWS_AS(CovarianceMatrix::from_correlation(vols, wrong_size), std::invalid_argument);

    CHECK_NOTHROW(CovarianceMatrix::from_correlation(vols, corr));
}

TEST_CASE("CovarianceMatrix::from_correlation repairs a non-PSD correlation matrix",
          "[covariance][matrix]") {
    // Higham's classic non-PSD example, unit diagonal already.
    const std::array<double, 3> vols{1.0, 1.0, 1.0};
    Matrix corr(3, 3);
    corr << 1.0, 1.0, 0.0, 1.0, 1.0, 1.0, 0.0, 1.0, 1.0;

    CHECK_THROWS_AS(CovarianceMatrix::from_correlation(vols, corr), std::invalid_argument);

    const CovarianceMatrix cov =
        CovarianceMatrix::from_correlation(vols, corr, RepairPolicy::Repair);
    REQUIRE(cov.repair_report().has_value());
    CHECK(cov.repair_report()->min_eigenvalue_before < 0.0);
    CHECK(max_abs_diff(reconstructed(cov.cholesky()), cov.matrix()) < 1e-9);
}

TEST_CASE("CovarianceMatrix::factor(all) equals cholesky()", "[covariance][matrix]") {
    Matrix sigma(3, 3);
    sigma << 4.0, 1.0, 0.5, 1.0, 3.0, 0.2, 0.5, 0.2, 2.0;
    const CovarianceMatrix cov = CovarianceMatrix::from_matrix(sigma);

    const std::array<std::size_t, 3> all{0, 1, 2};
    CHECK(max_abs_diff(cov.factor(all), cov.cholesky()) < 1e-12);
}

TEST_CASE("CovarianceMatrix::factor is the Cholesky of the principal submatrix (Option-1 "
          "invariance)",
          "[covariance][matrix]") {
    // A 3-asset Sigma; the {0,1} submatrix's Cholesky must match a
    // CovarianceMatrix built directly from that 2x2 principal submatrix.
    Matrix sigma3(3, 3);
    sigma3 << 4.0, 1.0, 0.5, 1.0, 3.0, 0.2, 0.5, 0.2, 2.0;
    const CovarianceMatrix cov3 = CovarianceMatrix::from_matrix(sigma3);

    Matrix sigma2(2, 2);
    sigma2 << 4.0, 1.0, 1.0, 3.0; // top-left principal submatrix of sigma3
    const CovarianceMatrix cov2 = CovarianceMatrix::from_matrix(sigma2);

    const std::array<std::size_t, 2> active{0, 1};
    CHECK(max_abs_diff(cov3.factor(active), cov2.cholesky()) < 1e-9);
}

TEST_CASE("CovarianceMatrix::factor throws on an out-of-range index", "[covariance][matrix]") {
    Matrix sigma(2, 2);
    sigma << 4.0, 1.0, 1.0, 3.0;
    const CovarianceMatrix cov = CovarianceMatrix::from_matrix(sigma);

    const std::array<std::size_t, 1> bad{2}; // dimension() == 2, valid indices are {0, 1}
    CHECK_THROWS_AS(cov.factor(bad), std::out_of_range);
}
