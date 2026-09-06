/// \file
/// Tests for UniformReal and UniformInt. UniformInt is constexpr end to end, so
/// its Lemire multiply-shift is pinned with hand-computed compile-time vectors;
/// UniformReal (non-constexpr ctor) is checked with the replay stub and over a
/// real engine stream.

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/dist/traits.hpp>
#include <math/dist/uniform.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/stream.hpp>

#include "word_stub.hpp"

namespace {

using mms::math::dist::Sampler;
using mms::math::dist::uniform_int;
using mms::math::dist::uniform_real;
using mms::math::dist::UniformInt;
using mms::math::dist::UniformReal;
using mms::math::testing::FixedWord;
using mms::math::testing::WordStub;
namespace rng = mms::math::rng;

static_assert(Sampler<UniformReal>);
static_assert(Sampler<UniformInt>);
static_assert(UniformReal::words_per_sample == 1);
static_assert(UniformInt::words_per_sample == 1);

// --- UniformInt compile-time vectors ------------------------------------------
//
// offset = mulhi64(word, range) = floor(word * range / 2^64), result = lo + offset.

consteval std::int64_t int_draw(std::int64_t lo, std::int64_t hi, std::uint64_t word) {
    FixedWord g{word};
    return UniformInt{lo, hi}(g);
}

// range 10: offset lands in [0, 10)
static_assert(int_draw(0, 9, 0) == 0);
static_assert(int_draw(0, 9, ~0ULL) == 9, "word just below 2^64 maps to the top bucket");
static_assert(int_draw(0, 9, 1ULL << 63) == 5, "half of 2^64 maps to floor(10/2)");
static_assert(int_draw(0, 9, (1ULL << 63) + (1ULL << 62)) == 7,
              "three quarters maps to floor(7.5)");

// same offsets, shifted by a negative lo
static_assert(int_draw(-5, 5, 0) == -5);
static_assert(int_draw(-5, 5, ~0ULL) == 5);
static_assert(int_draw(-5, 5, 1ULL << 63) == 0);

// degenerate range 1: mulhi64(word, 1) == 0, so it always returns lo (still one word)
static_assert(int_draw(7, 7, 0) == 7);
static_assert(int_draw(7, 7, ~0ULL) == 7);

// full 2^64 span: range wraps to 0, offset is the raw word, wrapped onto the type
static_assert(int_draw(INT64_MIN, INT64_MAX, 0) == INT64_MIN);
static_assert(int_draw(INT64_MIN, INT64_MAX, ~0ULL) == INT64_MAX);
static_assert(int_draw(INT64_MIN, INT64_MAX, 1ULL << 63) == 0);

} // namespace

TEST_CASE("UniformInt is Lemire multiply-shift with a one-word cost", "[dist][uniform]") {
    WordStub stub{0, ~0ULL, 1ULL << 63};
    const UniformInt d{0, 9};
    CHECK(d(stub) == 0);
    CHECK(d(stub) == 9);
    CHECK(d(stub) == 5);
    CHECK(stub.consumed() == 3);
}

TEST_CASE("UniformInt rejects lo > hi", "[dist][uniform]") {
    CHECK_THROWS_AS((UniformInt{5, 4}), std::invalid_argument);
    CHECK_NOTHROW((UniformInt{4, 4}));
}

TEST_CASE("UniformInt covers its range roughly uniformly", "[dist][uniform]") {
    const rng::RngHub hub{rng::Seed{0xD1CEULL}};
    auto engine =
        hub.engine_for({.subsystem = rng::Subsystem::AdverseFlow, .asset = rng::AssetKey{3}});
    const UniformInt die{1, 6};

    std::array<int, 6> hits{};
    constexpr int n = 120'000;
    bool in_range = true;
    for (int i = 0; i < n; ++i) {
        const std::int64_t v = die(engine);
        in_range = in_range && (v >= 1) && (v <= 6);
        if (v >= 1 && v <= 6) {
            ++hits[static_cast<std::size_t>(v - 1)];
        }
    }
    CHECK(in_range);

    const int expected = n / 6;
    bool all_faces_fair = true;
    for (const int h : hits) {
        all_faces_fair = all_faces_fair && h > (expected * 9) / 10 && h < (expected * 11) / 10;
    }
    CHECK(all_faces_fair);
}

TEST_CASE("UniformInt spans a wide range without escaping it", "[dist][uniform]") {
    const rng::RngHub hub{rng::Seed{0xBEEF'CAFEULL}};
    auto engine = hub.engine_for({.subsystem = rng::Subsystem::Latency, .asset = rng::AssetKey{5}});
    const UniformInt u{0, 999};

    std::int64_t lo_seen = 1000;
    std::int64_t hi_seen = -1;
    bool in_range = true;
    constexpr int n = 100'000;
    for (int i = 0; i < n; ++i) {
        const std::int64_t v = u(engine);
        in_range = in_range && (v >= 0) && (v <= 999);
        lo_seen = std::min(lo_seen, v);
        hi_seen = std::max(hi_seen, v);
    }
    CHECK(in_range);
    CHECK(lo_seen <= 5);
    CHECK(hi_seen >= 994);
}

TEST_CASE("UniformReal on [0, 1) is exactly canonical_double", "[dist][uniform]") {
    WordStub stub{0, 1ULL << 11, ~0ULL};
    const UniformReal u{0.0, 1.0};
    CHECK(u(stub) == 0.0);
    CHECK(u(stub) == 0x1.0p-53);
    CHECK(u(stub) == 1.0 - 0x1.0p-53);
    CHECK(stub.consumed() == 3);
}

TEST_CASE("UniformReal maps the canonical grid affinely onto [lo, hi)", "[dist][uniform]") {
    SECTION("midpoint") {
        WordStub stub{1ULL << 63}; // canonical_double == 0.5
        CHECK(UniformReal{-1.0, 1.0}(stub) == 0.0);
    }
    SECTION("endpoints") {
        WordStub stub{0, ~0ULL};
        const UniformReal u{10.0, 20.0};
        CHECK(u(stub) == 10.0);
        // The affine map lo + span * u can round up to exactly hi even though
        // u < 1, just like std::uniform_real_distribution. So [lo, hi] in the
        // worst case, never past hi.
        const double top = u(stub);
        CHECK(top <= 20.0);
        CHECK(top == Catch::Approx(20.0).epsilon(1e-12));
    }
}

TEST_CASE("UniformReal rejects non-finite or inverted bounds", "[dist][uniform]") {
    constexpr double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double dbl_max = std::numeric_limits<double>::max();

    CHECK_THROWS_AS((UniformReal{1.0, 0.0}), std::invalid_argument);
    CHECK_THROWS_AS((UniformReal{nan, 1.0}), std::invalid_argument);
    CHECK_THROWS_AS((UniformReal{0.0, inf}), std::invalid_argument);
    CHECK_THROWS_AS((UniformReal{-dbl_max, dbl_max}), std::invalid_argument); // hi - lo overflows
    CHECK_NOTHROW((UniformReal{4.0, 4.0}));
}

TEST_CASE("UniformReal with lo == hi is a point mass", "[dist][uniform]") {
    WordStub stub{0, 12'345, ~0ULL};
    const UniformReal u{4.0, 4.0};
    CHECK(u(stub) == 4.0);
    CHECK(u(stub) == 4.0);
    CHECK(u(stub) == 4.0);
}

TEST_CASE("UniformReal fills [lo, hi) with the right mean and variance", "[dist][uniform]") {
    const rng::RngHub hub{rng::Seed{0xA11CEULL}};
    auto engine = hub.engine_for({.subsystem = rng::Subsystem::Price, .asset = rng::AssetKey{4}});
    const UniformReal u{2.0, 5.0};

    constexpr int n = 100'000;
    bool in_range = true;
    double sum = 0.0;
    double sum_sq = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = u(engine);
        in_range = in_range && (x >= 2.0) && (x <= 5.0); // hi reachable by rounding
        sum += x;
        sum_sq += x * x;
    }
    const double mean = sum / n;
    const double var = (sum_sq / n) - (mean * mean);
    CHECK(in_range);
    CHECK(mean == Catch::Approx(3.5).margin(0.03));
    CHECK(var == Catch::Approx(0.75).margin(0.03)); // (5 - 2)^2 / 12
}

TEST_CASE("uniform_real / uniform_int free functions match their samplers", "[dist][uniform]") {
    WordStub a{0x1234'5678'9ABC'DEF0ULL};
    WordStub b{0x1234'5678'9ABC'DEF0ULL};
    CHECK(uniform_real(a, -3.0, 7.0) == UniformReal{-3.0, 7.0}(b));

    WordStub c{0xFEDC'BA98'7654'3210ULL};
    WordStub d{0xFEDC'BA98'7654'3210ULL};
    CHECK(uniform_int(c, -100, 100) == UniformInt{-100, 100}(d));
}
