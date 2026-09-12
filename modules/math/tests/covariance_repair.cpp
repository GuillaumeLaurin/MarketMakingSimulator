/// \file
/// Tests for the covariance repair primitives. clip_eigenvalues is pinned with
/// hand-computed spectral projections on a 2x2; nearest_correlation_higham is
/// checked against Higham's published example and for a strictly-PD result.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/covariance/repair.hpp>
#include <math/types.hpp>

#include <Eigen/Cholesky>
#include <Eigen/Eigenvalues>

namespace {

using mms::math::Matrix;
using mms::math::covariance::clip_eigenvalues;
using mms::math::covariance::nearest_correlation_higham;

double min_eig(const Matrix& m) {
    return Eigen::SelfAdjointEigenSolver<Matrix>(m, Eigen::EigenvaluesOnly)
        .eigenvalues()
        .minCoeff();
}

bool is_positive_definite(const Matrix& m) {
    return Eigen::LLT<Matrix>(m).info() == Eigen::Success;
}

double max_abs_diff(const Matrix& a, const Matrix& b) {
    return (a - b).cwiseAbs().maxCoeff();
}

} // namespace

TEST_CASE("clip_eigenvalues leaves an already-PD matrix alone", "[covariance][repair]") {
    Matrix a(3, 3);
    a << 1.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 3.0;

    const auto result = clip_eigenvalues(a, 0.0);
    CHECK(max_abs_diff(result.matrix, a) < 1e-12);
    CHECK(result.report.frobenius_distance < 1e-12);
    CHECK(result.report.min_eigenvalue_before == Catch::Approx(1.0));
    CHECK(result.report.min_eigenvalue_after == Catch::Approx(1.0));
    CHECK(result.report.iterations == 0);
}

TEST_CASE("clip_eigenvalues projects a known negative eigenvalue", "[covariance][repair]") {
    // a = [[2,3],[3,2]] has eigenvalues {5, -1} with eigenvectors (1,1)/sqrt2,
    // (1,-1)/sqrt2. Clipping to 0: repaired = 5 * (1,1)(1,1)^T / 2 = all 2.5.
    Matrix a(2, 2);
    a << 2.0, 3.0, 3.0, 2.0;

    SECTION("epsilon = 0") {
        const auto result = clip_eigenvalues(a, 0.0);
        Matrix expected(2, 2);
        expected << 2.5, 2.5, 2.5, 2.5;
        CHECK(max_abs_diff(result.matrix, expected) < 1e-12);
        CHECK(result.report.min_eigenvalue_before == Catch::Approx(-1.0));
        CHECK(result.report.min_eigenvalue_after == Catch::Approx(0.0).margin(1e-12));
        CHECK(result.report.frobenius_distance == Catch::Approx(1.0)); // |lambda_neg|
        CHECK(result.report.max_abs_entry_change == Catch::Approx(0.5));
        CHECK(result.report.iterations == 0);
    }
    SECTION("epsilon = 0.5 makes it strictly PD") {
        const auto result = clip_eigenvalues(a, 0.5);
        Matrix expected(2, 2);
        expected << 2.75, 2.25, 2.25, 2.75;
        CHECK(max_abs_diff(result.matrix, expected) < 1e-12);
        CHECK(result.report.min_eigenvalue_after == Catch::Approx(0.5));
        CHECK(result.report.frobenius_distance == Catch::Approx(1.5));
        CHECK(is_positive_definite(result.matrix));
        CHECK(min_eig(result.matrix) == Catch::Approx(0.5));
    }
}

TEST_CASE("nearest_correlation_higham matches Higham's published example", "[covariance][repair]") {
    // A is symmetric with unit diagonal but not PSD (eigenvalues 1 +/- sqrt 2, 1).
    Matrix a(3, 3);
    a << 1.0, 1.0, 0.0, 1.0, 1.0, 1.0, 0.0, 1.0, 1.0;
    REQUIRE(min_eig(a) < 0.0);

    const auto result = nearest_correlation_higham(a);

    // published nearest correlation matrix (Higham 2002)
    Matrix expected(3, 3);
    expected << 1.0, 0.7607, 0.1573, 0.7607, 1.0, 0.7607, 0.1573, 0.7607, 1.0;
    CHECK(max_abs_diff(result.matrix, expected) < 1e-3);

    // unit diagonal, symmetric, PSD
    CHECK(result.matrix.diagonal().isOnes(1e-12));
    CHECK(max_abs_diff(result.matrix, result.matrix.transpose()) < 1e-12);
    CHECK(min_eig(result.matrix) > -1e-8);

    CHECK(result.report.min_eigenvalue_before < 0.0);
    CHECK(result.report.min_eigenvalue_after > -1e-8);
    CHECK(result.report.iterations >= 1);
    CHECK(result.report.frobenius_distance > 0.0);
}

TEST_CASE("nearest_correlation_higham with a positive floor yields an LLT-able matrix",
          "[covariance][repair]") {
    Matrix a(3, 3);
    a << 1.0, 1.0, 0.0, 1.0, 1.0, 1.0, 0.0, 1.0, 1.0;

    const auto result = nearest_correlation_higham(a, 1e-6);
    CHECK(is_positive_definite(result.matrix));
    CHECK(min_eig(result.matrix) >= 1e-6 - 1e-9);
    CHECK(result.matrix.diagonal().isOnes(1e-9));
}

TEST_CASE("nearest_correlation_higham barely touches a valid correlation matrix",
          "[covariance][repair]") {
    Matrix r(3, 3);
    r << 1.0, 0.3, 0.2, 0.3, 1.0, 0.5, 0.2, 0.5, 1.0;
    REQUIRE(is_positive_definite(r));

    const auto result = nearest_correlation_higham(r);
    CHECK(max_abs_diff(result.matrix, r) < 1e-9);
    CHECK(result.report.frobenius_distance < 1e-9);
    CHECK(result.report.iterations <= 3);
}
