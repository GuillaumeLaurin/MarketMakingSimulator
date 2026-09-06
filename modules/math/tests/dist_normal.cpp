/// \file
/// Tests for probit (AS241), StandardNormal and Normal. probit is checked on all
/// three branches with round targets (probit(Phi(k)) == k), plus oddness,
/// monotonicity and the NaN contract. The samplers are checked for exact
/// composition (StandardNormal == probit o open_unit_double) and over a real
/// engine stream for shape.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/dist/normal.hpp>
#include <math/dist/primitives.hpp>
#include <math/dist/traits.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/stream.hpp>

#include "word_stub.hpp"

namespace {

using mms::math::dist::Normal;
using mms::math::dist::normal;
using mms::math::dist::open_unit_double;
using mms::math::dist::probit;
using mms::math::dist::Sampler;
using mms::math::dist::standard_normal;
using mms::math::dist::StandardNormal;
using mms::math::testing::WordStub;
namespace rng = mms::math::rng;

static_assert(Sampler<StandardNormal>);
static_assert(Sampler<Normal>);
static_assert(StandardNormal::words_per_sample == 1);
static_assert(Normal::words_per_sample == 1);

// Standard-normal CDF values, so that probit(cdf_k) must return k.
constexpr double cdf_0_5 = 0.6914624612740131;
constexpr double cdf_1 = 0.8413447460685429;
constexpr double cdf_2 = 0.9772498680518208;
constexpr double cdf_3 = 0.9986501019683699;
constexpr double cdf_neg_7 = 1.279812543885835e-12;

} // namespace

TEST_CASE("probit inverts the normal CDF on the central branch", "[dist][normal][probit]") {
    CHECK(probit(0.5) == 0.0);
    CHECK(probit(cdf_0_5) == Catch::Approx(0.5).margin(1e-11));
    CHECK(probit(cdf_1) == Catch::Approx(1.0).margin(1e-11));
    CHECK(probit(1.0 - cdf_1) == Catch::Approx(-1.0).margin(1e-11));
}

TEST_CASE("probit inverts the normal CDF on the near-tail branch", "[dist][normal][probit]") {
    CHECK(probit(cdf_2) == Catch::Approx(2.0).margin(1e-11));
    CHECK(probit(cdf_3) == Catch::Approx(3.0).margin(1e-11));
    CHECK(probit(0.975) == Catch::Approx(1.959963984540054).margin(1e-11));
    CHECK(probit(0.025) == Catch::Approx(-1.959963984540054).margin(1e-11));
}

TEST_CASE("probit inverts the normal CDF on the far-tail branch", "[dist][normal][probit]") {
    CHECK(probit(cdf_neg_7) == Catch::Approx(-7.0).margin(1e-8));
    CHECK(probit(1.0 - cdf_neg_7) == Catch::Approx(7.0).margin(1e-8));
}

TEST_CASE("probit is odd up to the rounding of 1 - p", "[dist][normal][probit]") {
    bool odd = true;
    for (const double p : {0.01, 0.05, 0.2, 0.4, 0.45, 0.499}) {
        odd = odd && (probit(p) == Catch::Approx(-probit(1.0 - p)).margin(1e-12));
    }
    CHECK(odd);
}

TEST_CASE("probit is strictly increasing", "[dist][normal][probit]") {
    constexpr int n = 401;
    const double lo = 1e-6;
    const double step = (1.0 - (2.0 * lo)) / (n - 1);

    bool increasing = true;
    double previous = probit(lo);
    for (int i = 1; i < n; ++i) {
        const double current = probit(lo + (i * step));
        increasing = increasing && (current > previous);
        previous = current;
    }
    CHECK(increasing);
}

TEST_CASE("probit returns NaN outside (0, 1) and never throws", "[dist][normal][probit]") {
    CHECK(std::isnan(probit(0.0)));
    CHECK(std::isnan(probit(1.0)));
    CHECK(std::isnan(probit(-0.1)));
    CHECK(std::isnan(probit(1.5)));
    CHECK(std::isnan(probit(std::numeric_limits<double>::quiet_NaN())));
}

TEST_CASE("StandardNormal is exactly probit composed with open_unit_double", "[dist][normal]") {
    const std::uint64_t words[] = {
        0ULL, 1ULL, 0x8000'0000'0000'0000ULL, 0xDEAD'BEEF'FEED'FACEULL, ~0ULL};
    bool matches = true;
    for (const std::uint64_t w : words) {
        WordStub a{w};
        WordStub b{w};
        matches = matches && (StandardNormal{}(a) == probit(open_unit_double(b)));
    }
    CHECK(matches);
}

TEST_CASE("StandardNormal and Normal consume exactly one word", "[dist][normal]") {
    WordStub stub{1, 2};
    (void)StandardNormal{}(stub);
    CHECK(stub.consumed() == 1);
    (void)Normal{3.0, 2.0}(stub);
    CHECK(stub.consumed() == 2);
}

TEST_CASE("Normal is mu + sigma * StandardNormal", "[dist][normal]") {
    bool matches = true;
    for (const std::uint64_t w : {7ULL, 0x1234'5678'9ABC'DEF0ULL, ~0ULL - 3ULL}) {
        WordStub a{w};
        WordStub b{w};
        matches = matches && (Normal{2.5, 1.5}(a) == 2.5 + (1.5 * StandardNormal{}(b)));
    }
    CHECK(matches);
}

TEST_CASE("Normal rejects non-finite mu or sigma <= 0", "[dist][normal]") {
    constexpr double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();

    CHECK_THROWS_AS((Normal{0.0, 0.0}), std::invalid_argument);
    CHECK_THROWS_AS((Normal{0.0, -1.0}), std::invalid_argument);
    CHECK_THROWS_AS((Normal{nan, 1.0}), std::invalid_argument);
    CHECK_THROWS_AS((Normal{0.0, inf}), std::invalid_argument);
    CHECK_NOTHROW((Normal{-5.0, 2.0}));
}

TEST_CASE("StandardNormal has the right shape over a real engine stream", "[dist][normal]") {
    rng::RngHub hub{rng::Seed{0x5EED'1234ULL}};
    auto engine = hub.engine_for({.subsystem = rng::Subsystem::Price, .asset = rng::AssetKey{7}});
    const StandardNormal z;

    constexpr int n = 200'000;
    double sum = 0.0;
    double sum_sq = 0.0;
    int within_1sigma = 0;
    int within_2sigma = 0;
    double lo_seen = 0.0;
    double hi_seen = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = z(engine);
        sum += x;
        sum_sq += x * x;
        within_1sigma += static_cast<int>(std::abs(x) <= 1.0);
        within_2sigma += static_cast<int>(std::abs(x) <= 2.0);
        lo_seen = std::min(lo_seen, x);
        hi_seen = std::max(hi_seen, x);
    }
    const double mean = sum / n;
    const double var = (sum_sq / n) - (mean * mean);

    CHECK(mean == Catch::Approx(0.0).margin(0.02));
    CHECK(var == Catch::Approx(1.0).margin(0.03));
    CHECK(static_cast<double>(within_1sigma) / n == Catch::Approx(0.6827).margin(0.01));
    CHECK(static_cast<double>(within_2sigma) / n == Catch::Approx(0.9545).margin(0.01));
    CHECK(lo_seen < -3.0);
    CHECK(hi_seen > 3.0);
}

TEST_CASE("Normal shifts and scales the standard shape", "[dist][normal]") {
    rng::RngHub hub{rng::Seed{0x5EED'ABCDULL}};
    auto engine =
        hub.engine_for({.subsystem = rng::Subsystem::AdverseFlow, .asset = rng::AssetKey{8}});
    const Normal d{10.0, 2.0};

    constexpr int n = 100'000;
    double sum = 0.0;
    double sum_sq = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = d(engine);
        sum += x;
        sum_sq += x * x;
    }
    const double mean = sum / n;
    const double stddev = std::sqrt((sum_sq / n) - (mean * mean));

    CHECK(mean == Catch::Approx(10.0).margin(0.05));
    CHECK(stddev == Catch::Approx(2.0).margin(0.05));
}

TEST_CASE("standard_normal / normal free functions match their samplers", "[dist][normal]") {
    WordStub a{0x0F1E'2D3C'4B5A'6879ULL};
    WordStub b{0x0F1E'2D3C'4B5A'6879ULL};
    CHECK(standard_normal(a) == StandardNormal{}(b));

    WordStub c{0xC0FF'EE00'1234'5678ULL};
    WordStub e{0xC0FF'EE00'1234'5678ULL};
    CHECK(normal(c, -2.0, 3.0) == Normal{-2.0, 3.0}(e));
}
