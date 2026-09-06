/// \file
/// Tests for Bernoulli and Poisson. Both must consume exactly one engine word
/// whatever their parameter (the consumption invariant that lets a sweep keep
/// the stream aligned). Bernoulli is a threshold on canonical_double; Poisson is
/// CDF inversion, pinned against an independent reimplementation of the walk and
/// against hand vectors, then checked for shape over a real engine stream.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/dist/discrete.hpp>
#include <math/dist/primitives.hpp>
#include <math/dist/traits.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/stream.hpp>

#include "word_stub.hpp"

namespace {

using mms::math::dist::Bernoulli;
using mms::math::dist::bernoulli;
using mms::math::dist::canonical_double;
using mms::math::dist::Poisson;
using mms::math::dist::poisson;
using mms::math::dist::Sampler;
using mms::math::testing::WordStub;
namespace rng = mms::math::rng;

static_assert(std::is_same_v<Bernoulli::result_type, bool>);
static_assert(std::is_same_v<Poisson::result_type, std::int64_t>);
static_assert(Sampler<Bernoulli>);
static_assert(Sampler<Poisson>);
static_assert(Bernoulli::words_per_sample == 1);
static_assert(Poisson::words_per_sample == 1);

// Independent reimplementation of the Poisson CDF walk, for cross-checking.
std::int64_t inverse_cdf_poisson(double mean, double u) {
    const double p0 = std::exp(-mean);
    double cdf = p0;
    double term = p0;
    std::int64_t k = 0;
    while (u > cdf && k < 512) {
        ++k;
        term *= mean / static_cast<double>(k);
        cdf += term;
    }
    return k;
}

} // namespace

TEST_CASE("Bernoulli is a threshold on canonical_double", "[dist][discrete][bernoulli]") {
    WordStub t{0};          // canonical_double == 0.0
    WordStub f{1ULL << 63}; // canonical_double == 0.5
    WordStub g{~0ULL};      // canonical_double == 1 - 2^-53
    CHECK(Bernoulli{0.5}(t));
    CHECK_FALSE(Bernoulli{0.5}(f)); // 0.5 < 0.5 is false
    CHECK_FALSE(Bernoulli{0.5}(g));
}

TEST_CASE("Bernoulli never short-circuits its parameter", "[dist][discrete][bernoulli]") {
    // p == 0 is always false, p == 1 always true, but each still draws one word.
    WordStub a{~0ULL};
    CHECK_FALSE(Bernoulli{0.0}(a));
    CHECK(a.consumed() == 1);

    WordStub b{0};
    CHECK(Bernoulli{1.0}(b));
    CHECK(b.consumed() == 1);
}

TEST_CASE("Bernoulli rejects p outside [0, 1]", "[dist][discrete][bernoulli]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((Bernoulli{-0.1}), std::invalid_argument);
    CHECK_THROWS_AS((Bernoulli{1.1}), std::invalid_argument);
    CHECK_THROWS_AS((Bernoulli{nan}), std::invalid_argument);
    CHECK_NOTHROW((Bernoulli{0.0}));
    CHECK_NOTHROW((Bernoulli{1.0}));
    CHECK_NOTHROW((Bernoulli{0.5}));
}

TEST_CASE("Bernoulli matches its probability over a real engine stream",
          "[dist][discrete][bernoulli]") {
    const rng::RngHub hub{rng::Seed{0xB33'0001ULL}};
    auto engine =
        hub.engine_for({.subsystem = rng::Subsystem::AdverseFlow, .asset = rng::AssetKey{11}});
    const Bernoulli d{0.3};

    constexpr int n = 200'000;
    int trues = 0;
    for (int i = 0; i < n; ++i) {
        trues += static_cast<int>(d(engine));
    }
    CHECK(static_cast<double>(trues) / n == Catch::Approx(0.3).margin(0.005));
}

TEST_CASE("bernoulli free function matches the sampler", "[dist][discrete][bernoulli]") {
    WordStub a{0x2468'ACE0'1357'9BDFULL};
    WordStub b{0x2468'ACE0'1357'9BDFULL};
    CHECK(bernoulli(a, 0.42) == Bernoulli{0.42}(b));
}

TEST_CASE("Poisson inverts the CDF of one uniform: hand vectors", "[dist][discrete][poisson]") {
    WordStub zero{0}; // u == 0.0 -> k == 0 for any mean
    CHECK(Poisson{5.0}(zero) == 0);
    WordStub half{1ULL << 63}; // u == 0.5; F(0) = e^-1 ~ 0.368 < 0.5 <= F(1)
    CHECK(Poisson{1.0}(half) == 1);
}

TEST_CASE("Poisson matches an independent CDF walk", "[dist][discrete][poisson]") {
    const std::array<std::uint64_t, 7> words{0ULL,
                                             1ULL << 60,
                                             1ULL << 62,
                                             1ULL << 63,
                                             0xC000'0000'0000'0000ULL,
                                             0xFFF0'0000'0000'0000ULL,
                                             ~0ULL};
    bool matches = true;
    for (const std::uint64_t w : words) {
        for (const double mean : {0.5, 3.0, 12.0}) {
            WordStub a{w};
            WordStub b{w};
            matches =
                matches && (Poisson{mean}(a) == inverse_cdf_poisson(mean, canonical_double(b)));
        }
    }
    CHECK(matches);
}

TEST_CASE("Poisson draws one word whatever the count it returns", "[dist][discrete][poisson]") {
    WordStub stub{~0ULL, ~0ULL}; // large u -> large k, still one word each
    const Poisson d{20.0};
    const std::int64_t k0 = d(stub);
    CHECK(stub.consumed() == 1);
    CHECK(k0 > 0);
    (void)d(stub);
    CHECK(stub.consumed() == 2);
}

TEST_CASE("Poisson rejects mean outside (0, 30]", "[dist][discrete][poisson]") {
    constexpr double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((Poisson{0.0}), std::invalid_argument);
    CHECK_THROWS_AS((Poisson{-1.0}), std::invalid_argument);
    CHECK_THROWS_AS((Poisson{30.001}), std::invalid_argument);
    CHECK_THROWS_AS((Poisson{inf}), std::invalid_argument);
    CHECK_THROWS_AS((Poisson{nan}), std::invalid_argument);
    CHECK_NOTHROW((Poisson{0.001}));
    CHECK_NOTHROW((Poisson{30.0}));
    CHECK_NOTHROW((Poisson{15.0}));
}

TEST_CASE("Poisson has mean == variance over a real engine stream", "[dist][discrete][poisson]") {
    const rng::RngHub hub{rng::Seed{0xB33'0002ULL}};
    auto engine =
        hub.engine_for({.subsystem = rng::Subsystem::Latency, .asset = rng::AssetKey{12}});
    const Poisson d{4.0};

    constexpr int n = 200'000;
    double sum = 0.0;
    double sum_sq = 0.0;
    int zeros = 0;
    bool non_negative = true;
    for (int i = 0; i < n; ++i) {
        const std::int64_t k = d(engine);
        non_negative = non_negative && (k >= 0);
        const auto x = static_cast<double>(k);
        sum += x;
        sum_sq += x * x;
        zeros += static_cast<int>(k == 0);
    }
    const double mean = sum / n;
    const double var = (sum_sq / n) - (mean * mean);

    CHECK(non_negative);
    CHECK(mean == Catch::Approx(4.0).margin(0.03));
    CHECK(var == Catch::Approx(4.0).margin(0.1));
    CHECK(static_cast<double>(zeros) / n == Catch::Approx(std::exp(-4.0)).margin(0.005));
}

TEST_CASE("Poisson works right up against the mean cap", "[dist][discrete][poisson]") {
    const rng::RngHub hub{rng::Seed{0xB33'0003ULL}};
    auto engine = hub.engine_for({.subsystem = rng::Subsystem::Price, .asset = rng::AssetKey{13}});
    const Poisson d{25.0};

    constexpr int n = 100'000;
    double sum = 0.0;
    double sum_sq = 0.0;
    std::int64_t max_k = 0;
    for (int i = 0; i < n; ++i) {
        const std::int64_t k = d(engine);
        max_k = (k > max_k) ? k : max_k;
        const auto x = static_cast<double>(k);
        sum += x;
        sum_sq += x * x;
    }
    const double mean = sum / n;
    const double var = (sum_sq / n) - (mean * mean);

    CHECK(mean == Catch::Approx(25.0).margin(0.1));
    CHECK(var == Catch::Approx(25.0).margin(0.5));
    CHECK(max_k < 512); // never near the loop guard
}

TEST_CASE("poisson free function matches the sampler", "[dist][discrete][poisson]") {
    WordStub a{0x9BDF'1357'ACE0'2468ULL};
    WordStub b{0x9BDF'1357'ACE0'2468ULL};
    CHECK(poisson(a, 6.0) == Poisson{6.0}(b));
}
