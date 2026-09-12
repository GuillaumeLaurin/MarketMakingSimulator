#ifndef MMS_MATH_PROCESS_CORRELATED_RANDOM_WALK_HPP
#define MMS_MATH_PROCESS_CORRELATED_RANDOM_WALK_HPP

/// \file
/// CorrelatedRandomWalk: the default PriceProcess. An arithmetic random walk
/// across N correlated assets, driven by correlated_normal_increment. Owns
/// its RNG engines by value (one per asset); reset() restores both prices and
/// engine state to what they were at construction.

#include <utility>
#include <vector>

#include <math/math_export.hpp>
#include <math/process/price_process.hpp>
#include <math/rng/engine.hpp>
#include <math/types.hpp>

namespace mms::math::process {

class MMS_MATH_EXPORT CorrelatedRandomWalk final : public PriceProcess {
public:
    /// chol is the already-factorised lower-triangular L of the active
    /// asset subset's covariance (e.g. CovarianceMatrix::factor(active) or
    /// ::cholesky()), never a raw Sigma. Throws std::invalid_argument unless
    /// mu.size() == chol.rows() == chol.cols() == engines.size() ==
    /// initial_prices.size().
    CorrelatedRandomWalk(Vector initial_prices,
                         Vector mu,
                         Matrix chol,
                         std::vector<rng::RngEngine> engines);

    [[nodiscard]] const Vector& prices() const noexcept override { return prices_; }

    /// Restores prices_ and the RNG engines to their state at construction.
    void reset() override;

    [[nodiscard]] Index asset_count() const noexcept override { return prices_.size(); }

protected:
    void do_advance(double dt) override;

private:
    Vector initial_prices_;
    Vector mu_;
    Matrix chol_;
    std::vector<rng::RngEngine> initial_engines_;
    std::vector<rng::RngEngine> engines_;
    Vector prices_;
};

} // namespace mms::math::process

#endif // MMS_MATH_PROCESS_CORRELATED_RANDOM_WALK_HPP