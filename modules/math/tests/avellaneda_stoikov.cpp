/// \file
/// Tests for the Avellaneda-Stoikov formulas. Low-level functions (taking
/// sigma_q or k directly) are checked against hand-computed values; the
/// high-level CovarianceMatrix-based overloads are checked to compose the
/// low-level ones over a hand-computed Sigma*q. Also covers the AS6
/// cross-asset invariant, gamma/k validation, and liquidity_half_spread's
/// no-broadcast contract.

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/avellaneda_stoikov.hpp>
#include <math/covariance/covariance_matrix.hpp>

namespace {

using mms::math::Matrix;
using mms::math::Vector;
using mms::math::covariance::CovarianceMatrix;
namespace as = mms::math::avellaneda_stoikov;

double max_abs_diff(const Vector& a, const Vector& b) {
    return (a - b).cwiseAbs().maxCoeff();
}

} // namespace

TEST_CASE("AvellanedaStoikovParams is a plain aggregate", "[avellaneda_stoikov]") {
    const as::AvellanedaStoikovParams params{.gamma = 0.5, .k = Vector::Constant(1, 1.5)};
    CHECK(params.gamma == 0.5);
    CHECK(params.k(0) == 1.5);
}

TEST_CASE("risk_skew(gamma, tau, sigma_q) is gamma * tau * sigma_q", "[avellaneda_stoikov]") {
    Vector sigma_q(3);
    sigma_q << 1.0, -2.0, 3.0;

    const Vector actual = as::risk_skew(3.0, 0.5, sigma_q); // gamma*tau == 1.5

    Vector expected(3);
    expected << 1.5, -3.0, 4.5;
    CHECK(max_abs_diff(actual, expected) < 1e-15);
}

TEST_CASE("inventory_risk(q, sigma_q) is q . sigma_q", "[avellaneda_stoikov]") {
    Vector q(2);
    q << 1.0, 2.0;
    Vector sigma_q(2);
    sigma_q << 3.0, 4.0;

    CHECK(as::inventory_risk(q, sigma_q) == Catch::Approx(11.0)); // 1*3 + 2*4
}

TEST_CASE("risk_skew(gamma, tau, sigma, q) composes the low-level overload over Sigma*q",
          "[avellaneda_stoikov]") {
    Matrix sigma(2, 2);
    sigma << 4.0, 2.0, 2.0, 3.0;
    const CovarianceMatrix cov = CovarianceMatrix::from_matrix(sigma);

    Vector q(2);
    q << 1.0, -1.0;

    const Vector actual = as::risk_skew(2.0, 0.25, cov, q); // gamma*tau == 0.5

    // Sigma*q = [4*1 + 2*-1, 2*1 + 3*-1] = [2, -1]; risk_skew = 0.5 * [2, -1]
    Vector expected(2);
    expected << 1.0, -0.5;
    CHECK(max_abs_diff(actual, expected) < 1e-12);

    // must also match the low-level overload fed the same Sigma*q
    const Vector sigma_q = cov.matrix() * q;
    CHECK(max_abs_diff(actual, as::risk_skew(2.0, 0.25, sigma_q)) < 1e-12);
}

TEST_CASE("inventory_risk(sigma, q) composes the low-level overload over Sigma*q",
          "[avellaneda_stoikov]") {
    Matrix sigma(2, 2);
    sigma << 4.0, 2.0, 2.0, 3.0;
    const CovarianceMatrix cov = CovarianceMatrix::from_matrix(sigma);

    Vector q(2);
    q << 1.0, -1.0;

    // q^T Sigma q = q . [2, -1] = 1*2 + (-1)*(-1) = 3
    CHECK(as::inventory_risk(cov, q) == Catch::Approx(3.0));
    CHECK(as::inventory_risk(cov, q) == Catch::Approx(as::inventory_risk(q, cov.matrix() * q)));
}

TEST_CASE("reservation_price is s - risk_skew(gamma, tau, sigma, q)", "[avellaneda_stoikov]") {
    Matrix sigma(2, 2);
    sigma << 4.0, 2.0, 2.0, 3.0;
    const CovarianceMatrix cov = CovarianceMatrix::from_matrix(sigma);

    Vector s(2);
    s << 100.0, 50.0;
    Vector q(2);
    q << 1.0, -1.0;

    const Vector actual = as::reservation_price(s, 2.0, 0.25, cov, q);

    // risk_skew here is [1.0, -0.5] (see the risk_skew test above)
    Vector expected(2);
    expected << 99.0, 50.5;
    CHECK(max_abs_diff(actual, expected) < 1e-12);
}

TEST_CASE("reservation_price: positive correlation skews a correlated asset's price too "
          "(AS6 cross-asset invariant)",
          "[avellaneda_stoikov]") {
    // Assets 1, 2 positively correlated; going long asset 1 only (q = [1, 0])
    // must still drop asset 2's reservation price below its spot, because
    // (Sigma*q)_2 > 0 through the positive off-diagonal term.
    Matrix sigma(2, 2);
    sigma << 4.0, 2.0, 2.0, 3.0;
    const CovarianceMatrix cov = CovarianceMatrix::from_matrix(sigma);

    Vector s(2);
    s << 100.0, 100.0;
    Vector q(2);
    q << 1.0, 0.0;

    const Vector r = as::reservation_price(s, 1.0, 1.0, cov, q);

    CHECK(r(1) < s(1)); // sell-skew on the uninvolved but correlated asset
}

TEST_CASE("liquidity_half_spread is (1/gamma) * ln(1 + gamma/k) element-wise",
          "[avellaneda_stoikov]") {
    Vector k(3);
    k << 1.0, 2.0, 4.0;
    constexpr double gamma = 2.0;

    const Vector actual = as::liquidity_half_spread(gamma, k);

    Vector expected(3);
    for (int i = 0; i < 3; ++i) {
        expected(i) = (1.0 / gamma) * std::log(1.0 + (gamma / k(i)));
    }
    CHECK(max_abs_diff(actual, expected) < 1e-15);
}

TEST_CASE("liquidity_half_spread never broadcasts: result size always matches k's",
          "[avellaneda_stoikov]") {
    const Vector k1 = Vector::Constant(1, 1.5);
    const Vector result1 = as::liquidity_half_spread(1.0, k1);
    CHECK(result1.size() == 1);
    CHECK(result1(0) == Catch::Approx(std::log(1.0 + (1.0 / 1.5))));

    Vector k3(3);
    k3 << 1.5, 1.5, 1.5;
    const Vector result3 = as::liquidity_half_spread(1.0, k3);
    CHECK(result3.size() == 3);
}

TEST_CASE("optimal_total_spread is gamma*sigma2*tau + (2/gamma)*ln(1 + gamma/k)",
          "[avellaneda_stoikov]") {
    constexpr double gamma = 0.1;
    constexpr double sigma2 = 4.0;
    constexpr double tau = 1.0;
    constexpr double k = 1.5;

    const double actual = as::optimal_total_spread(gamma, sigma2, tau, k);
    const double expected = (gamma * sigma2 * tau) + ((2.0 / gamma) * std::log(1.0 + (gamma / k)));

    CHECK(actual == Catch::Approx(expected));
}

TEST_CASE("avellaneda_stoikov functions reject a non-finite or non-positive gamma",
          "[avellaneda_stoikov]") {
    const Vector sigma_q = Vector::Constant(1, 1.0);
    const Vector k = Vector::Constant(1, 1.0);
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    CHECK_THROWS_AS(as::risk_skew(0.0, 1.0, sigma_q), std::invalid_argument);
    CHECK_THROWS_AS(as::risk_skew(-1.0, 1.0, sigma_q), std::invalid_argument);
    CHECK_THROWS_AS(as::risk_skew(nan, 1.0, sigma_q), std::invalid_argument);
    CHECK_THROWS_AS(as::liquidity_half_spread(0.0, k), std::invalid_argument);
    CHECK_THROWS_AS(as::optimal_total_spread(0.0, 1.0, 1.0, 1.0), std::invalid_argument);

    Matrix sigma = Matrix::Identity(1, 1);
    const CovarianceMatrix cov = CovarianceMatrix::from_matrix(sigma);
    const Vector q = Vector::Constant(1, 1.0);
    CHECK_THROWS_AS(as::risk_skew(0.0, 1.0, cov, q), std::invalid_argument);
    CHECK_THROWS_AS(as::reservation_price(q, 0.0, 1.0, cov, q), std::invalid_argument);
}

TEST_CASE("avellaneda_stoikov functions reject a non-finite or non-positive k",
          "[avellaneda_stoikov]") {
    Vector bad_k(2);
    bad_k << 1.0, 0.0;
    CHECK_THROWS_AS(as::liquidity_half_spread(1.0, bad_k), std::invalid_argument);

    bad_k << 1.0, -1.0;
    CHECK_THROWS_AS(as::liquidity_half_spread(1.0, bad_k), std::invalid_argument);

    bad_k << 1.0, std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(as::liquidity_half_spread(1.0, bad_k), std::invalid_argument);

    CHECK_THROWS_AS(as::optimal_total_spread(1.0, 1.0, 1.0, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(as::optimal_total_spread(1.0, 1.0, 1.0, -1.0), std::invalid_argument);
}

TEST_CASE("inventory_risk never validates q or sigma_q (no gamma/k involved)",
          "[avellaneda_stoikov]") {
    // q is allowed to be short (negative), zero, or anything finite: no
    // precondition to violate here.
    Vector q(2);
    q << -5.0, 0.0;
    Vector sigma_q(2);
    sigma_q << -1.0, 2.0;

    CHECK_NOTHROW(as::inventory_risk(q, sigma_q));
}
