/// \file
/// Tests for Exponential. The inverse-CDF transform is pinned exactly against
/// -log(open_unit_double(g)) / rate; shape (mean, variance, median) and the
/// memoryless property are checked over a real engine stream.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/dist/exponential.hpp>
#include <math/dist/primitives.hpp>
#include <math/dist/sampler.hpp>
#include <math/dist/traits.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/stream.hpp>

#include "word_stub.hpp"

namespace {

using mms::math::dist::Exponential;
using mms::math::dist::exponential;
using mms::math::dist::Gamma;
using mms::math::dist::gamma;
using mms::math::dist::open_unit_double;
using mms::math::dist::Sampler;
using mms::math::dist::StandardGamma;
using mms::math::testing::WordStub;
namespace rng = mms::math::rng;

static_assert(Sampler<Exponential>);
static_assert(Sampler<StandardGamma>);
static_assert(Sampler<Gamma>);
static_assert(Exponential::words_per_sample == 1);
static_assert(StandardGamma::words_per_sample == -1);
static_assert(Gamma::words_per_sample == -1);
static_assert(std::is_same_v<Gamma::result_type, double>);

// discard() is defined only for fixed-consumption samplers, so it must be a
// compile error for the two rejection-based gamma samplers.
template<class S, class G>
concept DiscardCallable =
    requires(const S& s, G& g) { mms::math::dist::discard(s, g, std::size_t{1}); };
static_assert(DiscardCallable<Exponential, rng::RngEngine>);
static_assert(!DiscardCallable<StandardGamma, rng::RngEngine>);
static_assert(!DiscardCallable<Gamma, rng::RngEngine>);

rng::RngEngine make_engine(std::uint64_t tag) {
    const rng::RngHub hub{rng::Seed{tag}};
    return hub.engine_for({.subsystem = rng::Subsystem::Price, .asset = rng::AssetKey{20}});
}

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

TEST_CASE("StandardGamma rejects shape < 1 or non-finite", "[dist][gamma]") {
    constexpr double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((StandardGamma{0.5}), std::invalid_argument);
    CHECK_THROWS_AS((StandardGamma{0.0}), std::invalid_argument);
    CHECK_THROWS_AS((StandardGamma{nan}), std::invalid_argument);
    CHECK_THROWS_AS((StandardGamma{inf}), std::invalid_argument);
    CHECK_NOTHROW((StandardGamma{1.0}));
    CHECK_NOTHROW((StandardGamma{2.5}));
    CHECK_NOTHROW((StandardGamma{50.0}));
}

TEST_CASE("StandardGamma has mean and variance equal to its shape", "[dist][gamma]") {
    auto engine = make_engine(0x6A33'0001ULL);
    bool ok = true;
    for (const double shape : {1.0, 2.0, 5.0, 12.0}) {
        const StandardGamma d{shape};
        CHECK(d.shape() == shape);

        constexpr int n = 150'000;
        double sum = 0.0;
        double sum_sq = 0.0;
        bool positive = true;
        for (int i = 0; i < n; ++i) {
            const double x = d(engine);
            positive = positive && (x > 0.0);
            sum += x;
            sum_sq += x * x;
        }
        const double mean = sum / n;
        const double var = (sum_sq / n) - (mean * mean);
        CHECK(positive);
        // Gamma(shape, 1): mean == variance == shape.
        ok = ok && (std::abs(mean - shape) < 0.05 * shape);
        ok = ok && (std::abs(var - shape) < 0.15 * shape);
    }
    CHECK(ok);
}

TEST_CASE("StandardGamma is deterministic for a fixed engine state", "[dist][gamma]") {
    auto a = make_engine(0x6A33'0002ULL);
    auto b = a;
    const StandardGamma d{3.5};
    bool same = true;
    for (int i = 0; i < 1000; ++i) {
        same = same && (d(a) == d(b));
    }
    CHECK(same);
}

TEST_CASE("Gamma rejects shape <= 0 or scale <= 0 or non-finite", "[dist][gamma]") {
    constexpr double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((Gamma{0.0, 1.0}), std::invalid_argument);
    CHECK_THROWS_AS((Gamma{-1.0, 1.0}), std::invalid_argument);
    CHECK_THROWS_AS((Gamma{1.0, 0.0}), std::invalid_argument);
    CHECK_THROWS_AS((Gamma{1.0, -2.0}), std::invalid_argument);
    CHECK_THROWS_AS((Gamma{nan, 1.0}), std::invalid_argument);
    CHECK_THROWS_AS((Gamma{1.0, inf}), std::invalid_argument);
    CHECK_NOTHROW((Gamma{0.3, 1.0}));
    CHECK_NOTHROW((Gamma{1.0, 1.0}));
    CHECK_NOTHROW((Gamma{4.0, 2.0}));
}

TEST_CASE("Gamma has mean shape * scale and variance shape * scale^2", "[dist][gamma]") {
    auto engine = make_engine(0x6A33'0003ULL);

    struct Case {
        double shape;
        double scale;
    };

    bool ok = true;
    for (const Case c : {Case{0.4, 2.0}, Case{1.0, 3.0}, Case{4.0, 0.5}, Case{9.0, 1.5}}) {
        const Gamma d{c.shape, c.scale};
        CHECK(d.shape() == c.shape);
        CHECK(d.scale() == c.scale);

        constexpr int n = 200'000;
        double sum = 0.0;
        double sum_sq = 0.0;
        bool positive = true;
        for (int i = 0; i < n; ++i) {
            const double x = d(engine);
            positive = positive && (x > 0.0);
            sum += x;
            sum_sq += x * x;
        }
        const double mean = sum / n;
        const double var = (sum_sq / n) - (mean * mean);
        const double expected_mean = c.shape * c.scale;
        const double expected_var = c.shape * c.scale * c.scale;
        CHECK(positive);
        ok = ok && (std::abs(mean - expected_mean) < 0.05 * expected_mean);
        ok = ok && (std::abs(var - expected_var) < 0.20 * expected_var);
    }
    CHECK(ok);
}

TEST_CASE("Gamma(1, scale) has the exponential shape", "[dist][gamma]") {
    auto engine = make_engine(0x6A33'0004ULL);
    const double scale = 2.5;
    const Gamma d{1.0, scale};

    constexpr int n = 200'000;
    double sum = 0.0;
    double sum_sq = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = d(engine);
        sum += x;
        sum_sq += x * x;
    }
    const double mean = sum / n;
    const double var = (sum_sq / n) - (mean * mean);
    // Exponential(rate = 1 / scale): mean == scale, variance == scale^2.
    CHECK(mean == Catch::Approx(scale).margin(0.05));
    CHECK(var == Catch::Approx(scale * scale).margin(0.35));
}

TEST_CASE("gamma free function matches the sampler", "[dist][gamma]") {
    auto a = make_engine(0x6A33'0005ULL);
    auto b = a;
    CHECK(gamma(a, 2.0, 1.5) == Gamma{2.0, 1.5}(b));

    auto c = make_engine(0x6A33'0006ULL);
    auto e = c;
    CHECK(gamma(c, 0.6, 3.0) == Gamma{0.6, 3.0}(e));
}
