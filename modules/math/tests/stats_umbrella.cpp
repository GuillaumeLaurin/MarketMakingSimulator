/// \file
/// The <math/stats.hpp> umbrella pulls in every public symbol of the stats layer.

#include <array>
#include <cmath>
#include <span>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/stats.hpp>

TEST_CASE("<math/stats.hpp> exposes the whole stats layer", "[stats][umbrella]") {
    using namespace mms::math::stats;

    // running_stats.hpp
    RunningStats rs;
    rs.add(1.0);
    rs.add(3.0);
    CHECK(rs.mean() == 2.0);
    const RunningStats combined = merged(rs, RunningStats{});
    CHECK(combined.count() == 2);

    // drawdown.hpp
    DrawdownTracker dd;
    dd.add(100.0);
    dd.add(80.0);
    CHECK(dd.max_drawdown() == 20.0);

    // ewma.hpp
    EwmaEstimator ew{10.0};
    ew.add(5.0);
    CHECK(ew.mean() == 5.0);

    // quantile.hpp
    const std::array<double, 3> sorted{1.0, 2.0, 3.0};
    CHECK(interpolated_quantile(sorted, 0.5) == 2.0);
    const P2State ps = P2State::from_sorted(0.5, {1.0, 2.0, 3.0, 4.0, 5.0});
    CHECK(ps.estimate() == 3.0);
    PSquareQuantile pq{0.5};
    pq.add(1.0);
    CHECK(pq.value() == 1.0);

    // traits.hpp + volatility.hpp
    static_assert(VolatilityEstimator<ConstantVolEstimator>);
    static_assert(VolatilityEstimator<EwmaVolEstimator>);
    static_assert(VolatilityEstimator<RollingVolEstimator>);
    static_assert(VolatilityEstimator<AnyVolatilityEstimator>);

    const ConstantVolEstimator cv{2.0};
    CHECK(cv.sigma() == 2.0);
    EwmaVolEstimator ev{8.0};
    ev.update(1.0);
    CHECK(ev.sample_count() == 1);
    RollingVolEstimator rv{4};
    rv.update(1.0);
    rv.update(3.0);
    CHECK(std::isfinite(rv.sigma()));
    const AnyVolatilityEstimator any{ConstantVolEstimator{3.0}};
    CHECK(any.sigma() == 3.0);

    // batch.hpp
    const std::array<double, 4> xs{1.0, 2.0, 3.0, 4.0};
    CHECK(mean(xs) == 2.5);
    CHECK(sample_variance(xs) == Catch::Approx(5.0 / 3.0));
    CHECK(quantile(xs, 0.5) == Catch::Approx(2.5));
    CHECK(std::isfinite(sharpe_ratio(xs)));

    const std::array<double, 4> equity{100.0, 120.0, 90.0, 130.0};
    CHECK(max_drawdown(equity) == Catch::Approx(30.0));
}
