/// \file
///  Implementation of correlated_normal_increment (<math/process/increment.hpp>).

#include <cmath>
#include <cstddef>

#include <math/dist/normal.hpp>
#include <math/process/increment.hpp>

namespace mms::math::process {

Vector
correlated_normal_increment(const Matrix& chol, std::span<rng::RngEngine> engines, double dt) {
    const auto n = static_cast<Index>(engines.size());
    Vector z(n);
    for (Index i = 0; i < n; ++i) {
        z(i) = dist::standard_normal(engines[static_cast<std::size_t>(i)]);
    }
    return chol.triangularView<Eigen::Lower>() * z * std::sqrt(dt);
}

} // namespace mms::math::process