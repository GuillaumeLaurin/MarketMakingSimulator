/// \file
/// Implementation of CorrelatedRandomWalk
/// (<math/process/correlated_random_walk.hpp>).

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

} // namespace

CorrelatedRandomWalk::CorrelatedRandomWalk(Vector initial_prices,
                                           Vector mu,
                                           Matrix chol,
                                           std::vector<rng::RngEngine> engines) {
    validate(initial_prices, mu, chol, engines);

    initial_prices_ = std::move(initial_prices);
    mu_ = std::move(mu);
    chol_ = std::move(chol);
    initial_engines_ = engines;
    engines_ = std::move(engines);
    prices_ = initial_prices_;
}

void CorrelatedRandomWalk::do_advance(double dt) {
    prices_ += (mu_ * dt) + correlated_normal_increment(chol_, engines_, dt);
}

void CorrelatedRandomWalk::reset() {
    prices_ = initial_prices_;
    engines_ = initial_engines_;
}

} // namespace mms::math::process