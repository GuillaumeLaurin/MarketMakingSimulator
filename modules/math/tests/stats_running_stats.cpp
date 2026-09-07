/// \file
/// Tests for RunningStats. The weighted Welford recursion is checked against an
/// independent two-pass computation; reliability-weight semantics are pinned
/// against hand values; merge() is checked for equivalence with a single stream
/// and for purity of the free merged().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/stats/running_stats.hpp>

namespace {

using mms::math::stats::merged;
using mms::math::stats::RunningStats;

constexpr double inf = std::numeric_limits<double>::infinity();
const double nan_value = std::numeric_limits<double>::quiet_NaN();

// Independent two-pass reference for an unweighted sample.
struct TwoPass {
    std::int64_t n = 0;
    double mean = 0.0;
    double m2 = 0.0;
    double lo = 0.0;
    double hi = 0.0;
};

TwoPass two_pass(std::span<const double> xs) {
    TwoPass r;
    r.n = static_cast<std::int64_t>(xs.size());
    double sum = 0.0;
    r.lo = xs[0];
    r.hi = xs[0];
    for (const double x : xs) {
        sum += x;
        r.lo = std::min(r.lo, x);
        r.hi = std::max(r.hi, x);
    }
    r.mean = sum / static_cast<double>(xs.size());
    for (const double x : xs) {
        r.m2 += (x - r.mean) * (x - r.mean);
    }
    return r;
}

// The canonical little dataset: mean 5, m2 32, so pop var 4, sample var 32/7.
constexpr std::array<double, 8> data{2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};

} // namespace

TEST_CASE("RunningStats is all-NaN and zero-count when empty", "[stats][running]") {
    const RunningStats s;
    CHECK(s.count() == 0);
    CHECK(s.weight_sum() == 0.0);
    CHECK(std::isnan(s.mean()));
    CHECK(std::isnan(s.population_variance()));
    CHECK(std::isnan(s.sample_variance()));
    CHECK(std::isnan(s.stddev()));
    CHECK(std::isnan(s.min()));
    CHECK(std::isnan(s.max()));
}

TEST_CASE("RunningStats with one sample has zero population variance, NaN sample variance",
          "[stats][running]") {
    RunningStats s;
    s.add(5.0);
    CHECK(s.count() == 1);
    CHECK(s.weight_sum() == 1.0);
    CHECK(s.mean() == 5.0);
    CHECK(s.min() == 5.0);
    CHECK(s.max() == 5.0);
    CHECK(s.population_variance() == 0.0);
    CHECK(std::isnan(s.sample_variance()));
    CHECK(std::isnan(s.stddev()));
}

TEST_CASE("RunningStats matches a two-pass computation (unweighted)", "[stats][running]") {
    RunningStats s;
    for (const double x : data) {
        s.add(x);
    }
    const TwoPass ref = two_pass(data);

    CHECK(s.count() == ref.n);
    CHECK(s.weight_sum() == Catch::Approx(static_cast<double>(ref.n)));
    CHECK(s.mean() == Catch::Approx(ref.mean));
    CHECK(s.min() == ref.lo);
    CHECK(s.max() == ref.hi);
    // unweighted: eff dof == n - 1, so sample_variance == m2 / (n - 1).
    CHECK(s.population_variance() == Catch::Approx(ref.m2 / static_cast<double>(ref.n)));
    CHECK(s.sample_variance() == Catch::Approx(ref.m2 / static_cast<double>(ref.n - 1)));
    CHECK(s.stddev() == Catch::Approx(std::sqrt(ref.m2 / static_cast<double>(ref.n - 1))));
    // known exact-ish values for this dataset
    CHECK(s.population_variance() == Catch::Approx(4.0));
    CHECK(s.sample_variance() == Catch::Approx(32.0 / 7.0));
}

TEST_CASE("RunningStats reliability weights: weight w equals w unit observations for the mean",
          "[stats][running]") {
    RunningStats weighted;
    weighted.add(1.0, 2.0);
    weighted.add(2.0, 3.0);
    weighted.add(3.0, 5.0);

    RunningStats repeated;
    for (int i = 0; i < 2; ++i) {
        repeated.add(1.0);
    }
    for (int i = 0; i < 3; ++i) {
        repeated.add(2.0);
    }
    for (int i = 0; i < 5; ++i) {
        repeated.add(3.0);
    }

    // weighted mean = (2*1 + 3*2 + 5*3) / 10 = 2.3; m2 = 6.10; pop var = 0.61
    CHECK(weighted.weight_sum() == Catch::Approx(10.0));
    CHECK(weighted.mean() == Catch::Approx(2.3));
    CHECK(weighted.population_variance() == Catch::Approx(0.61));

    // the mean and population variance match the repeated-unit form ...
    CHECK(repeated.mean() == Catch::Approx(weighted.mean()));
    CHECK(repeated.population_variance() == Catch::Approx(weighted.population_variance()));

    // ... but count() and the sample-variance denominator differ.
    CHECK(weighted.count() == 3);
    CHECK(repeated.count() == 10);
    // weighted eff dof = 10 - (4 + 9 + 25) / 10 = 6.2 ; repeated eff dof = 10 - 1 = 9
    CHECK(weighted.sample_variance() == Catch::Approx(6.10 / 6.2));
    CHECK(repeated.sample_variance() == Catch::Approx(6.10 / 9.0));
}

TEST_CASE("RunningStats ignores non-finite or non-positive samples", "[stats][running]") {
    RunningStats s;
    s.add(1.0);
    s.add(3.0);
    const std::int64_t n0 = s.count();
    const double mean0 = s.mean();
    const double pvar0 = s.population_variance();

    s.add(nan_value);
    s.add(inf);
    s.add(-inf);
    s.add(2.0, 0.0);
    s.add(2.0, -1.0);
    s.add(2.0, nan_value);
    s.add(2.0, inf);

    CHECK(s.count() == n0);
    CHECK(s.mean() == mean0);
    CHECK(s.population_variance() == pvar0);
    CHECK(s.weight_sum() == 2.0);
}

TEST_CASE("RunningStats tracks min and max across signs", "[stats][running]") {
    RunningStats s;
    for (const double x : {3.0, -7.5, 0.0, 12.25, -1.0, 4.0}) {
        s.add(x);
    }
    CHECK(s.min() == -7.5);
    CHECK(s.max() == 12.25);
}

TEST_CASE("RunningStats::merge equals a single stream over the whole dataset", "[stats][running]") {
    RunningStats whole;
    for (const double x : data) {
        whole.add(x);
    }

    RunningStats left;
    for (std::size_t i = 0; i < 4; ++i) {
        left.add(data[i]);
    }
    RunningStats right;
    for (std::size_t i = 4; i < data.size(); ++i) {
        right.add(data[i]);
    }
    left.merge(right);

    CHECK(left.count() == whole.count());
    CHECK(left.weight_sum() == Catch::Approx(whole.weight_sum()));
    CHECK(left.mean() == Catch::Approx(whole.mean()).margin(1e-12));
    CHECK(left.population_variance() == Catch::Approx(whole.population_variance()).margin(1e-12));
    CHECK(left.sample_variance() == Catch::Approx(whole.sample_variance()).margin(1e-12));
    CHECK(left.min() == whole.min());
    CHECK(left.max() == whole.max());
}

TEST_CASE("RunningStats::merge with an empty operand is a no-op / a copy", "[stats][running]") {
    RunningStats populated;
    for (const double x : data) {
        populated.add(x);
    }

    SECTION("merging in an empty accumulator changes nothing") {
        RunningStats s = populated;
        s.merge(RunningStats{});
        CHECK(s.count() == populated.count());
        CHECK(s.mean() == populated.mean());
        CHECK(s.population_variance() == populated.population_variance());
        CHECK(s.min() == populated.min());
        CHECK(s.max() == populated.max());
    }
    SECTION("merging into an empty accumulator copies the other") {
        RunningStats s;
        s.merge(populated);
        CHECK(s.count() == populated.count());
        CHECK(s.mean() == populated.mean());
        CHECK(s.population_variance() == populated.population_variance());
        CHECK(s.min() == populated.min());
        CHECK(s.max() == populated.max());
    }
}

TEST_CASE("merged() is a pure combine", "[stats][running]") {
    RunningStats a;
    for (std::size_t i = 0; i < 4; ++i) {
        a.add(data[i]);
    }
    RunningStats b;
    for (std::size_t i = 4; i < data.size(); ++i) {
        b.add(data[i]);
    }

    const double a_mean_before = a.mean();
    const double b_mean_before = b.mean();

    const RunningStats c = merged(a, b);

    CHECK(a.mean() == a_mean_before); // operands untouched
    CHECK(b.mean() == b_mean_before);

    RunningStats reference = a;
    reference.merge(b);
    CHECK(c.count() == reference.count());
    CHECK(c.mean() == reference.mean());
    CHECK(c.population_variance() == reference.population_variance());
}

TEST_CASE("merged() is commutative up to rounding", "[stats][running]") {
    RunningStats a;
    a.add(1.0, 2.0);
    a.add(10.0, 1.0);
    a.add(-3.0, 4.0);
    RunningStats b;
    b.add(7.0, 3.0);
    b.add(0.5, 1.0);

    const RunningStats ab = merged(a, b);
    const RunningStats ba = merged(b, a);

    CHECK(ab.count() == ba.count());
    CHECK(ab.weight_sum() == Catch::Approx(ba.weight_sum()));
    CHECK(ab.mean() == Catch::Approx(ba.mean()).margin(1e-12));
    CHECK(ab.population_variance() == Catch::Approx(ba.population_variance()).margin(1e-10));
    CHECK(ab.min() == ba.min());
    CHECK(ab.max() == ba.max());
}

TEST_CASE("RunningStats is deterministic for a fixed add order", "[stats][running]") {
    RunningStats a;
    RunningStats b;
    for (int i = 0; i < 5000; ++i) {
        const double x = std::sin(0.1 * i) * 3.0 + 0.001 * i;
        const double w = 1.0 + 0.5 * (i % 3);
        a.add(x, w);
        b.add(x, w);
    }
    CHECK(a.mean() == b.mean());
    CHECK(a.population_variance() == b.population_variance());
    CHECK(a.sample_variance() == b.sample_variance());
    CHECK(a.min() == b.min());
    CHECK(a.max() == b.max());
}
