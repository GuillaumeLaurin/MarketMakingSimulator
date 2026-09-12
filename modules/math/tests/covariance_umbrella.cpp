/// \file
/// The <math/covariance.hpp> umbrella pulls in every public symbol of the
/// covariance layer.

#include <array>
#include <cstddef>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/covariance.hpp>

TEST_CASE("<math/covariance.hpp> exposes the whole covariance layer", "[covariance][umbrella]") {
    using namespace mms::math::covariance;

    // repair.hpp
    mms::math::Matrix a(2, 2);
    a << 2.0, 3.0, 3.0, 2.0; // eigenvalues {5, -1}: not PD

    const RepairResult clipped = clip_eigenvalues(a, 0.0);
    CHECK(clipped.report.min_eigenvalue_before == Catch::Approx(-1.0));

    const RepairResult nearest = nearest_correlation_higham(a);
    CHECK(nearest.report.iterations >= 1);

    static_assert(RepairPolicy::None != RepairPolicy::Repair);

    // covariance_matrix.hpp
    mms::math::Matrix sigma(2, 2);
    sigma << 4.0, 2.0, 2.0, 3.0;

    const CovarianceMatrix cov = CovarianceMatrix::from_matrix(sigma);
    CHECK(cov.dimension() == 2);
    CHECK_FALSE(cov.repair_report().has_value());

    const std::array<double, 2> vols{2.0, 3.0};
    mms::math::Matrix corr(2, 2);
    corr << 1.0, 0.2, 0.2, 1.0;
    const CovarianceMatrix from_corr = CovarianceMatrix::from_correlation(vols, corr);
    CHECK(from_corr.matrix()(0, 0) == Catch::Approx(4.0));

    const std::array<std::size_t, 1> active{0};
    CHECK(cov.factor(active).rows() == 1);
}
