/// \file
/// Implementation of CorrelatedRandomWalk
/// (<math/process/correlated_random_walk.hpp>).

#include <cmath>
#include <stdexcept>
#include <utility>

#include <math/process/correlated_random_walk.hpp>
#include <math/process/increment.hpp>

namespace mms::math::process {

namespace {

void validate(const Vector& initial_prices,
              const Vector& mu,
              const Matrix& chol,
              const std::vector<rng::RngEngine>& engines) {
    const Index n = initial_prices.size();
    const bool consistent = mu.size() == n && chol.rows() == n && chol.cols() == n &&
                            static_cast<Index>(engines.size()) == n;
    if (!consistent) {
        throw std::invalid_argument("CorrelatedRandomWalk: initial_prices, mu, chol and "
                                    "engines must all agreee on asset count");
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
                "CorrelatedRandomWalk: geometric dynamics needs finite initial prices > 0");
        }
    }
}

} // namespace

CorrelatedRandomWalk::CorrelatedRandomWalk(Vector initial_prices,
                                           Vector mu,
                                           Matrix chol,
                                           std::vector<rng::RngEngine> engines,
                                           Dynamics dynamics) {
    validate(initial_prices, mu, chol, engines);
    validate_prices(initial_prices, dynamics);

    initial_prices_ = std::move(initial_prices);
    mu_ = std::move(mu);
    chol_ = std::move(chol);
    dynamics_ = dynamics;
    const Matrix lower = chol_.triangularView<Eigen::Lower>();
    variance_ = lower.rowwise().squaredNorm();
    initial_engines_ = engines;
    engines_ = std::move(engines);
    prices_ = initial_prices_;
}

void CorrelatedRandomWalk::do_advance(double dt) {
    const Vector noise = correlated_normal_increment(chol_, engines_, dt);
    if (dynamics_ == Dynamics::Arithmetic) {
        prices_ += (mu_ * dt) + noise;
        return;
    }
    prices_.array() *= (((mu_ - 0.5 * variance_) * dt) + noise).array().exp();
}

void CorrelatedRandomWalk::reset() {
    prices_ = initial_prices_;
    engines_ = initial_engines_;
}

} // namespace mms::math::process