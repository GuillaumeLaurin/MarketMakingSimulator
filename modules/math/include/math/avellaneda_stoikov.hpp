#ifndef MMS_MATH_AVELLANEDA_STOIKOV_HPP
#define MMS_MATH_AVELLANEDA_STOIKOV_HPP

/// \file
/// Avellaneda-Stoikov (2008) market-making formulas: the paper-anchored,
/// unambiguous pieces only (reservation price, risk skew, inventory risk,
/// liquidity half-spread, and the scalar N=1 reference spread). Free
/// functions, not a class: mms composes the actual quoting rule from these.
/// Sign convention: q > 0 is long, q < 0 is short (both are the ordinary
/// state of a market maker mid-flow, never an error).

#include <cmath>
#include <stdexcept>

#include <math/covariance/covariance_matrix.hpp>
#include <math/types.hpp>

namespace mms::math::avellaneda_stoikov {

/// gamma: risk aversion. k: order-book liquidity/depth, one entry per asset
/// (or a single entry if every asset shares the same k - callers broadcast
/// themselves, math never guesses the target asset count). Plain aggregate,
/// no invariant enforced at construction; every function below validates
/// what it actually uses.
struct AvellanedaStoikovParams {
    double gamma;
    Vector k;
};

namespace detail {

inline void validate_gamma(double gamma) {
    if (!std::isfinite(gamma) || gamma <= 0.0) {
        throw std::invalid_argument("avellaneda_stoikov: gamma must be finite and > 0");
    }
}

inline void validate_k(const Vector& k) {
    for (Index i = 0; i < k.size(); ++i) {
        const double v = k(i);
        if (!std::isfinite(v) || v <= 0.0) {
            throw std::invalid_argument(
                "avellaneda_stoikov: every entry of k must be finite and > 0");
        }
    }
}

} // namespace detail

/// gamma * tau * sigma_q, with sigma_q == Sigma * q precomputed by the
/// caller. Testable without a CovarianceMatrix.
[[nodiscard]] inline Vector risk_skew(double gamma, double tau, const Vector& sigma_q) {
    detail::validate_gamma(gamma);
    return gamma * tau * sigma_q;
}

/// gamma * tau * (Sigma * q)
[[nodiscard]] inline Vector
risk_skew(double gamma, double tau, const covariance::CovarianceMatrix& sigma, const Vector& q) {
    return risk_skew(gamma, tau, sigma.matrix() * q);
}

/// q . sigma_q, with sigma_q == Sigma * q precomputed by the caller.
[[nodiscard]] inline double inventory_risk(const Vector& q, const Vector& sigma_q) {
    return q.dot(sigma_q);
}

/// q^T Sigma q.
[[nodiscard]] inline double inventory_risk(const covariance::CovarianceMatrix& sigma,
                                           const Vector& q) {
    return inventory_risk(q, sigma.matrix() * q);
}

/// s - risk_skew(gamma, tau, sigma, q)
[[nodiscard]] inline Vector reservation_price(const Vector& s,
                                              double gamma,
                                              double tau,
                                              const covariance::CovarianceMatrix& sigma,
                                              const Vector& q) {
    return s - risk_skew(gamma, tau, sigma, q);
}

/// (1/gamma) * ln(1 + gamma/k), element-wise. The result is the same size as
/// k; math never broadcasts a size-1 k to a larger asset count (there is no
/// other argument here to source that count from) - callers build
/// Vector::Constant(n, k_scalar) themselves when they want that.
[[nodiscard]] inline Vector liquidity_half_spread(double gamma, const Vector& k) {
    detail::validate_gamma(gamma);
    detail::validate_k(k);

    Vector result(k.size());
    for (Index i = 0; i < k.size(); ++i) {
        result(i) = (1.0 / gamma) * std::log(1.0 + (gamma / k(i)));
    }
    return result;
}

/// gamma * sigma2 * tau + (2/gamma) * ln(1 + gamma/k). The scalar, N=1
/// paper-anchored reference/test formula; mms composes the actual
/// multi-asset quoting rule from the pieces above rather than using this
/// directly.
[[nodiscard]] inline double
optimal_total_spread(double gamma, double sigma2, double tau, double k) {
    detail::validate_gamma(gamma);
    if (!std::isfinite(k) || k <= 0.0) {
        throw std::invalid_argument("avellaneda_stoikov: k must be finite and > 0");
    }
    return (gamma * sigma2 * tau) + ((2.0 / gamma) * std::log(1.0 + (gamma / k)));
}

} // namespace mms::math::avellaneda_stoikov

#endif // MMS_MATH_AVELLANEDA_STOIKOV_HPP