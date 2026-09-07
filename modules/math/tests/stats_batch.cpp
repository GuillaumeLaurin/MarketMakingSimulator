/// \file
/// Tests for the batch statistics. Hand-computed values on a small dataset,
/// quantiles cross-checked against numpy's "linear" percentile, and the
/// Sharpe / Sortino / max-drawdown formulas pinned on known returns.

#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/stats/batch.hpp>

namespace {

namespace batch = mms::math::stats;

const double nan_value = std::numeric_limits<double>::quiet_NaN();

// mean 5, m2 32 -> pop var 4, sample var 32/7.
constexpr std::array<double, 8> data{2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};

std::span<const double> span_of(const std::vector<double>& v) {
    return std::span<const double>{v.data(), v.size()};
}

} // namespace

TEST_CASE("batch mean / variance / stddev", "[stats][batch]") {
    CHECK(batch::mean(data) == 5.0);
    CHECK(batch::population_variance(data) == Catch::Approx(4.0));
    CHECK(batch::sample_variance(data) == Catch::Approx(32.0 / 7.0));
    CHECK(batch::stddev(data) == Catch::Approx(std::sqrt(32.0 / 7.0)));

    CHECK(std::isnan(batch::mean(std::span<const double>{})));
    CHECK(std::isnan(batch::population_variance(std::span<const double>{})));
    CHECK(std::isnan(batch::sample_variance(std::span<const double>{})));

    const std::array<double, 1> one{7.0};
    CHECK(batch::population_variance(one) == 0.0);
    CHECK(std::isnan(batch::sample_variance(one))); // n < 2
}

TEST_CASE("batch quantile does type-7 interpolation on a sorted copy", "[stats][batch]") {
    CHECK(batch::quantile(data, 0.0) == 2.0);
    CHECK(batch::quantile(data, 1.0) == 9.0);
    CHECK(batch::quantile(data, 0.25) == Catch::Approx(4.0)); // numpy percentile 25
    CHECK(batch::quantile(data, 0.5) == Catch::Approx(4.5));  // numpy percentile 50
    CHECK(batch::quantile(data, 0.9) == Catch::Approx(7.6));  // 7 + 0.3 * (9 - 7)

    // the input need not be sorted
    const std::array<double, 8> shuffled{9.0, 2.0, 5.0, 4.0, 4.0, 7.0, 4.0, 5.0};
    CHECK(batch::quantile(shuffled, 0.5) == Catch::Approx(4.5));

    CHECK(std::isnan(batch::quantile(std::span<const double>{}, 0.5)));
    CHECK(std::isnan(batch::quantile(data, -0.1)));
    CHECK(std::isnan(batch::quantile(data, 1.1)));
    CHECK(std::isnan(batch::quantile(data, nan_value)));
}

TEST_CASE("batch quantiles reads several probabilities off one sort", "[stats][batch]") {
    const std::array<double, 3> ps{0.0, 0.5, 1.0};
    const std::vector<double> got = batch::quantiles(data, ps);
    REQUIRE(got.size() == 3);
    CHECK(got[0] == Catch::Approx(2.0));
    CHECK(got[1] == Catch::Approx(4.5));
    CHECK(got[2] == Catch::Approx(9.0));

    const std::array<double, 2> ps2{0.25, 0.75};
    const std::vector<double> empty_in = batch::quantiles(std::span<const double>{}, ps2);
    REQUIRE(empty_in.size() == 2);
    CHECK(std::isnan(empty_in[0]));
    CHECK(std::isnan(empty_in[1]));

    CHECK(batch::quantiles(data, std::span<const double>{}).empty());
}

TEST_CASE("batch max_drawdown is the worst peak-to-trough drop", "[stats][batch]") {
    const std::array<double, 8> equity{100.0, 120.0, 90.0, 95.0, 130.0, 110.0, 105.0, 140.0};
    CHECK(batch::max_drawdown(equity) == Catch::Approx(30.0)); // 120 -> 90

    const std::array<double, 4> rising{1.0, 2.0, 3.0, 4.0};
    CHECK(batch::max_drawdown(rising) == 0.0);

    const std::array<double, 5> dip{10.0, 5.0, 8.0, 2.0, 6.0};
    CHECK(batch::max_drawdown(dip) == Catch::Approx(8.0)); // 10 -> 2

    const std::array<double, 1> one{5.0};
    CHECK(batch::max_drawdown(one) == 0.0);
    CHECK(std::isnan(batch::max_drawdown(std::span<const double>{})));
}

TEST_CASE("batch sharpe_ratio is (mean - rf) / stddev", "[stats][batch]") {
    const std::array<double, 5> r{0.01, 0.02, -0.01, 0.03, 0.0};

    CHECK(batch::sharpe_ratio(r) == Catch::Approx((batch::mean(r) - 0.0) / batch::stddev(r)));
    CHECK(batch::sharpe_ratio(r, 0.005) ==
          Catch::Approx((batch::mean(r) - 0.005) / batch::stddev(r)));

    const std::array<double, 3> flat{0.02, 0.02, 0.02};
    CHECK(std::isnan(batch::sharpe_ratio(flat))); // zero stddev

    const std::array<double, 1> one{0.01};
    CHECK(std::isnan(batch::sharpe_ratio(one))); // n < 2
    CHECK(std::isnan(batch::sharpe_ratio(std::span<const double>{})));
}

TEST_CASE("batch sortino_ratio uses the downside deviation", "[stats][batch]") {
    // r = {0.01, 0.02, -0.01, 0.03, -0.02}, rf = 0
    //   mean = 0.03 / 5 = 0.006
    //   downside sq = 0.01^2 + 0.02^2 = 5e-4 ; /5 = 1e-4 ; sqrt = 0.01
    //   sortino = 0.006 / 0.01 = 0.6
    const std::array<double, 5> r{0.01, 0.02, -0.01, 0.03, -0.02};
    CHECK(batch::sortino_ratio(r) == Catch::Approx(0.6));

    // no return below rf -> zero downside deviation -> NaN
    const std::array<double, 3> all_up{0.01, 0.02, 0.03};
    CHECK(std::isnan(batch::sortino_ratio(all_up)));

    CHECK(std::isnan(batch::sortino_ratio(std::span<const double>{})));
}

TEST_CASE("batch functions on a shared vector via span", "[stats][batch]") {
    const std::vector<double> v{3.0, 1.0, 4.0, 1.0, 5.0, 9.0, 2.0, 6.0};
    CHECK(batch::mean(span_of(v)) == Catch::Approx(31.0 / 8.0));
    CHECK(batch::quantile(span_of(v), 0.5) == Catch::Approx(3.5)); // sorted {1,1,2,3,4,5,6,9}
}
