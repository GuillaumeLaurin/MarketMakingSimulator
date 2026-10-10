#ifndef MMS_MATH_PROCESS_FACTOR_MODEL_HPP
#define MMS_MATH_PROCESS_FACTOR_MODEL_HPP

/// \file
/// FactorModel: the O(Nk) alternative to CorrelatedRandomWalk's O(N^2). Cross-
/// asset correlation is carried entirely by k common factors (beta_, N x k)
/// plus a per-asset idiosyncratic term; cheaper than a full N x N covariance
/// when k << N. Owns two independent engine groups by value: one per asset
/// (idiosyncratic draws) and one per factor (factor draws).

#include <stdexcept>
#include <utility>
#include <vector>

#include <math/math_export.hpp>
#include <math/process/dynamics.hpp>
#include <math/process/price_process.hpp>
#include <math/rng/engine.hpp>
#include <math/types.hpp>

namespace mms::math::process {

class MMS_MATH_EXPORT FactorModel final : public PriceProcess {
public:
    /// beta is N x k, factor_chol is the already-factorised k x k lower-
    /// triangular L of the factor covariance (never a raw factor Sigma; pass
    /// Matrix::Identity(k, k) for uncorrelated factors, there is no implicit
    /// default). idio_vol is per-asset, >= 0 (a zero entry means that asset's
    /// variance is fully explained by the factors). Throws
    /// std::invalid_argument unless initial_prices.size() == mu.size() ==
    /// beta.rows() == idio_vol.size() == engines.size() == N,
    /// beta.cols() == factor_chol.rows() == factor_chol.cols() ==
    /// factor_engines.size() == k, and every idio_vol entry is finite and
    /// >= 0, and, for Dynamics::Geometric, every initial price is finite and
    /// > 0.
    ///
    /// Each step draws f = correlated_normal_increment(factor_chol_,
    /// factor_engines_, dt) and idio_z, one standard_normal per asset engine,
    /// then forms noise = beta_ * f + idio_vol_.cwiseProduct(idio_z) * sqrt(dt).
    /// Arithmetic: prices_ += mu*dt + noise. Geometric: prices_ *=
    /// exp((mu - variance/2)*dt + noise), variance being the diagonal of
    /// beta L L^T beta^T plus idio_vol^2.
    FactorModel(Vector initial_prices,
                Vector mu,
                Matrix beta,
                Matrix factor_chol,
                Vector idio_vol,
                std::vector<rng::RngEngine> engines,
                std::vector<rng::RngEngine> factor_engines,
                Dynamics dynamics);

    [[nodiscard]] const Vector& prices() const noexcept override { return prices_; }

    /// Restores prices_ and both engine groups to their state at construction.
    void reset() override;

    [[nodiscard]] Index asset_count() const noexcept override { return prices_.size(); }

protected:
    void do_advance(double dt) override;

private:
    Vector initial_prices_;
    Vector mu_;
    Matrix beta_;
    Matrix factor_chol_;
    Vector idio_vol_;
    Dynamics dynamics_ = Dynamics::Arithmetic;
    Vector variance_; // diag(beta L L^T beta^T) + idio_vol^2
    std::vector<rng::RngEngine> initial_engines_;
    std::vector<rng::RngEngine> engines_;
    std::vector<rng::RngEngine> initial_factor_engines_;
    std::vector<rng::RngEngine> factor_engines_;
    Vector prices_;
};

} // namespace mms::math::process

#endif // MMS_MATH_PROCESS_FACTOR_MODEL_HPP