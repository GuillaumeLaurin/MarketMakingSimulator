/// \file
/// Tests for Exponential. The inverse-CDF transform is pinned exactly against
/// -log(open_unit_double(g)) / rate; shape (mean, variance, median) and the
/// memoryless property are checked over a real engine stream.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/dist/exponential.hpp>
#include <math/dist/primitives.hpp>
#include <math/dist/traits.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/stream.hpp>

#include "word_stub.hpp"

namespace {

using mms::math::dist::Exponential;
using mms::math::dist::exponential;
using mms::math::dist::open_unit_double;
using mms::math::dist::Sampler;
using mms::math::testing::WordStub;
namespace rng = mms::math::rng;

static_assert(Sampler<Exponential>);
static_assert(Exponential::words_per_sample == 1);

} // namespace

TEST_CASE("Exponential is exactly -log(open_unit_double) / rate", "[dist][exponential]") {
    const std::array<std::uint64_t, 5> words{
        0ULL, 1ULL, 0x8000'0000'0000'0000ULL, 0xABCD'1234'5678'9ABCULL, ~0ULL};
    bool matches = true;
    for (const std::uint64_t w : words) {
        for (const double rate : {0.25, 1.0, 7.5}) {
            WordStub a{w};
            WordStub b{w};
            matches = matches && (Exponential{rate}(a) == -std::log(open_unit_double(b)) / rate);
        }
    }
    CHECK(matches);
}

TEST_CASE("Exponential midpoint word gives ln(2) / rate", "[dist][exponential]") {
    WordStub s1{1ULL << 63}; // open_unit_double ~ 0.5
    WordStub s2{1ULL << 63};
    CHECK(Exponential{1.0}(s1) == Catch::Approx(0.6931471805599453).margin(1e-12));
    CHECK(Exponential{2.0}(s2) == Catch::Approx(0.6931471805599453 / 2.0).margin(1e-12));
}

TEST_CASE("Exponential consumes exactly one word and stays positive", "[dist][exponential]") {
    WordStub stub{1, 2, 3};
    const Exponential d{3.0};
    const double x0 = d(stub);
    CHECK(stub.consumed() == 1);
    CHECK(x0 > 0.0);
    CHECK(std::isfinite(x0));
    (void)d(stub);
    (void)d(stub);
    CHECK(stub.consumed() == 3);
}

TEST_CASE("Exponential rejects a non-finite or non-positive rate", "[dist][exponential]") {
    constexpr double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();

    CHECK_THROWS_AS((Exponential{0.0}), std::invalid_argument);
    CHECK_THROWS_AS((Exponential{-1.0}), std::invalid_argument);
    CHECK_THROWS_AS((Exponential{inf}), std::invalid_argument);
    CHECK_THROWS_AS((Exponential{nan}), std::invalid_argument);
    CHECK_NOTHROW((Exponential{2.5}));
}

TEST_CASE("Exponential has the right mean, variance and median", "[dist][exponential]") {
    const rng::RngHub hub{rng::Seed{0xE47D'0001ULL}};
    auto engine = hub.engine_for({.subsystem = rng::Subsystem::Latency, .asset = rng::AssetKey{9}});
    const Exponential d{0.5}; // mean 2, variance 4, median 2 * ln 2

    constexpr int n = 300'000;
    double sum = 0.0;
    double sum_sq = 0.0;
    int above_median = 0;
    bool all_positive = true;
    const double median = std::numbers::ln2 / 0.5;
    for (int i = 0; i < n; ++i) {
        const double x = d(engine);
        all_positive = all_positive && (x > 0.0);
        sum += x;
        sum_sq += x * x;
        above_median += static_cast<int>(x > median);
    }
    const double mean = sum / n;
    const double var = (sum_sq / n) - (mean * mean);

    CHECK(all_positive);
    CHECK(mean == Catch::Approx(2.0).margin(0.03));
    CHECK(var == Catch::Approx(4.0).margin(0.2));
    CHECK(static_cast<double>(above_median) / n == Catch::Approx(0.5).margin(0.01));
}

TEST_CASE("Exponential is memoryless", "[dist][exponential]") {
    const rng::RngHub hub{rng::Seed{0xE47D'0002ULL}};
    auto engine = hub.engine_for({.subsystem = rng::Subsystem::Price, .asset = rng::AssetKey{10}});
    const Exponential d{1.0};

    constexpr int n = 400'000;
    int above_s = 0;
    int above_s_plus_t = 0;
    for (int i = 0; i < n; ++i) {
        const double x = d(engine);
        if (x > 1.0) {
            ++above_s;
            if (x > 2.0) {
                ++above_s_plus_t;
            }
        }
    }
    // P(X > s + t | X > s) == P(X > t) == exp(-rate * t) == exp(-1)
    CHECK(static_cast<double>(above_s_plus_t) / above_s ==
          Catch::Approx(std::exp(-1.0)).margin(0.01));
}

TEST_CASE("exponential free function matches the sampler", "[dist][exponential]") {
    WordStub a{0x1357'9BDF'2468'ACE0ULL};
    WordStub b{0x1357'9BDF'2468'ACE0ULL};
    CHECK(exponential(a, 4.0) == Exponential{4.0}(b));
}
