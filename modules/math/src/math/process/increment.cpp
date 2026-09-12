/// \file
///  Implementation of correlated_normal_increment (<math/process/increment.hpp>).

#include <cmath>
#include <cstddef>
#include <stdexcept>

#include <math/dist/normal.hpp>
#include <math/process/increment.hpp>

namespace mms::math::process {

Vector
correlated_normal_increment(const Matrix& chol, std::span<rng::RngEngine> engines, double dt) {
    if (!(dt >= 0.0)) {
        throw std::invalid_argument("correlated_normal_increment: dt must be finite and >= 0");
    }

    const auto n = static_cast<Index>(engines.size());
    Vector z(n);
    for (Index i = 0; i < n; ++i) {
        z(i) = dist::standard_normal(engines[static_cast<std::size_t>(i)]);
    }
    return chol.triangularView<Eigen::Lower>() * z * std::sqrt(dt);
}

} // namespace mms::math::process