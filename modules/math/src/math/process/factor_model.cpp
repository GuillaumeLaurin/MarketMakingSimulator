/// \file
/// Implementation of FactorModel (<math/process/factor_model.hpp>).

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include <math/dist/normal.hpp>
#include <math/process/factor_model.hpp>
#include <math/process/increment.hpp>

namespace mms::math::process {

namespace {

void validate_size(const Vector& initial_prices,
                   const Vector& mu,
                   const Matrix& beta,
                   const Matrix& factor_chol,
                   const Vector& idio_vol,
                   const std::vector<rng::RngEngine>& engines,
                   const std::vector<rng::RngEngine>& factor_engines) {
    const Index n = initial_prices.size();
    const Index k = beta.cols();
    const bool asset_dims_ok = mu.size() == n && beta.rows() == n && idio_vol.size() == n &&
                               static_cast<Index>(engines.size()) == n;
    const bool factor_dims_ok = factor_chol.rows() == k && factor_chol.cols() == k &&
                                static_cast<Index>(factor_engines.size()) == k;
    if (!asset_dims_ok || !factor_dims_ok) {
        throw std::invalid_argument(
            "FactorModel: initial_prices, mu, beta, idio_vol and engines must agree on asset "
            "count, and beta, factor_chol and factor_engines must agree on factor count");
    }
}

void validate_idio_vol(const Vector& idio_vol) {
    for (Index i = 0; i < idio_vol.size(); ++i) {
        const double v = idio_vol(i);
        if (!std::isfinite(v) || v < 0.0) {
            throw std::invalid_argument("FactorModel: idio_vol entries must be finite and >= 0");
        }
    }
}

void validate_prices(const Vector& initial_prices, Dynamics dynamics) {
    if (dynamics != Dynamics::Geometric) {
        return;
    }
    for (Index i = 0; i < initial_prices.size(); ++i) {
        const double p = initial_prices(i);
        if (!std::isfinite(p) || p <= 0.0) {
            throw std::invalid_argument(
                "FactorModel: geometric dynamics needs finite initial prices > 0");
        }
    }
}

} // namespace

FactorModel::FactorModel(Vector initial_prices,
                         Vector mu,
                         Matrix beta,
                         Matrix factor_chol,
                         Vector idio_vol,
                         std::vector<rng::RngEngine> engines,
                         std::vector<rng::RngEngine> factor_engines,
                         Dynamics dynamics) {
    validate_size(initial_prices, mu, beta, factor_chol, idio_vol, engines, factor_engines);
    validate_idio_vol(idio_vol);
    validate_prices(initial_prices, dynamics);

    initial_prices_ = std::move(initial_prices);
    mu_ = std::move(mu);
    beta_ = std::move(beta);
    factor_chol_ = std::move(factor_chol);
    idio_vol_ = std::move(idio_vol);
    dynamics_ = dynamics;
    const Matrix loading = beta_ * factor_chol_.triangularView<Eigen::Lower>();
    variance_ = loading.rowwise().squaredNorm() + idio_vol_.cwiseAbs2();
    initial_engines_ = engines;
    engines_ = std::move(engines);
    initial_factor_engines_ = factor_engines;
    factor_engines_ = std::move(factor_engines);
    prices_ = initial_prices_;
}

void FactorModel::do_advance(double dt) {
    const Vector f = correlated_normal_increment(factor_chol_, factor_engines_, dt);

    const auto n = static_cast<Index>(engines_.size());
    Vector idio_z(n);
    for (Index i = 0; i < n; ++i) {
        idio_z(i) = dist::standard_normal(engines_[static_cast<std::size_t>(i)]);
    }
    const Vector factor_term = beta_ * f;
    const Vector idio_term = idio_vol_.cwiseProduct(idio_z) * std::sqrt(dt);
    if (dynamics_ == Dynamics::Arithmetic) {
        prices_ += (mu_ * dt) + factor_term + idio_term;
        return;
    }
    prices_.array() *= (((mu_ - 0.5 * variance_) * dt) + factor_term + idio_term).array().exp();
}

void FactorModel::reset() {
    prices_ = initial_prices_;
    engines_ = initial_engines_;
    factor_engines_ = initial_factor_engines_;
}

} // namespace mms::math::process