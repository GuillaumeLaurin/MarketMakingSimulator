/// \file
/// Tests for the dist primitives: canonical_double, open_unit_double,
/// uniform_bits. Each must consume exactly one engine word and land in the
/// documented interval; the exact grid points are pinned at compile time with a
/// constexpr word source, the interval bounds are re-checked over a real engine
/// stream.

#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/dist/primitives.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/stream.hpp>

#include "word_stub.hpp"

namespace {

using mms::math::dist::canonical_double;
using mms::math::dist::open_unit_double;
using mms::math::dist::Uint64Source;
using mms::math::dist::uniform_bits;
using mms::math::testing::FixedWord;
using mms::math::testing::WordStub;

// The concept accepts the production engine and both test stubs.
static_assert(Uint64Source<mms::math::rng::RngEngine>);
static_assert(Uint64Source<FixedWord>);
static_assert(Uint64Source<WordStub>);

// --- compile-time grid points ---------------------------------------------------

consteval double canon(std::uint64_t w) {
    FixedWord g{w};
    return canonical_double(g);
}
consteval double openu(std::uint64_t w) {
    FixedWord g{w};
    return open_unit_double(g);
}
consteval std::uint64_t bits(std::uint64_t w) {
    FixedWord g{w};
    return uniform_bits(g);
}

// canonical_double(g) == (g() >> 11) * 2^-53, on [0, 1)
static_assert(canon(0) == 0.0);
static_assert(canon((1ULL << 11) - 1) == 0.0, "the low 11 bits are discarded");
static_assert(canon(1ULL << 11) == 0x1.0p-53, "bit 11 is the least significant kept bit");
static_assert(canon(~0ULL) == 1.0 - 0x1.0p-53, "the largest output is one grid step below 1");
static_assert(canon(~0ULL) < 1.0);

// open_unit_double(g) == (double(g() >> 12) + 0.5) * 2^-52, strictly inside (0, 1)
static_assert(openu(0) == 0x1.0p-53, "smallest output is half a grid step above 0");
static_assert(openu(0) > 0.0);
static_assert(openu((1ULL << 12) - 1) == 0x1.0p-53, "the low 12 bits are discarded");
static_assert(openu(1ULL << 12) == 0x1.8p-52, "bit 12 is the least significant kept bit");
static_assert(openu(~0ULL) == 1.0 - 0x1.0p-53, "largest output is half a grid step below 1");
static_assert(openu(~0ULL) < 1.0);

// open_unit_double is symmetric about 1/2 for words whose low 12 bits are zero
static_assert(openu(0ULL) + openu(~(0ULL)) == 1.0);
static_assert(openu(123ULL << 12) + openu(~(123ULL << 12)) == 1.0);
static_assert(openu(4095ULL << 12) + openu(~(4095ULL << 12)) == 1.0);

// uniform_bits is the identity on the raw word
static_assert(bits(0) == 0);
static_assert(bits(~0ULL) == ~0ULL);
static_assert(bits(0xDEAD'BEEF'0BAD'F00DULL) == 0xDEAD'BEEF'0BAD'F00DULL);

} // namespace

TEST_CASE("each primitive consumes exactly one word", "[dist][primitives]") {
    WordStub stub{1, 2, 3};
    (void)uniform_bits(stub);
    CHECK(stub.consumed() == 1);
    (void)canonical_double(stub);
    CHECK(stub.consumed() == 2);
    (void)open_unit_double(stub);
    CHECK(stub.consumed() == 3);
}

TEST_CASE("uniform_bits replays the engine word for word", "[dist][primitives]") {
    WordStub stub{0x0123'4567'89AB'CDEFULL, 0xFFFF'0000'FFFF'0000ULL};
    CHECK(uniform_bits(stub) == 0x0123'4567'89AB'CDEFULL);
    CHECK(uniform_bits(stub) == 0xFFFF'0000'FFFF'0000ULL);
}

TEST_CASE("canonical_double stays in [0, 1) over a real engine stream", "[dist][primitives]") {
    mms::math::rng::RngHub hub{mms::math::rng::Seed{0xC0FFEEULL}};
    auto engine = hub.engine_for(
        {.subsystem = mms::math::rng::Subsystem::Price, .asset = mms::math::rng::AssetKey{1}});

    constexpr int n = 100'000;
    bool all_in_range = true;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double u = canonical_double(engine);
        all_in_range = all_in_range && (u >= 0.0) && (u < 1.0);
        sum += u;
    }
    CHECK(all_in_range);
    CHECK(sum / n == Catch::Approx(0.5).margin(0.01));
}

TEST_CASE("open_unit_double stays strictly inside (0, 1) over a real engine stream",
          "[dist][primitives]") {
    mms::math::rng::RngHub hub{mms::math::rng::Seed{0x1234'5678ULL}};
    auto engine = hub.engine_for({.subsystem = mms::math::rng::Subsystem::AdverseFlow,
                                  .asset = mms::math::rng::AssetKey{2}});

    constexpr int n = 100'000;
    bool all_strictly_inside = true;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double u = open_unit_double(engine);
        all_strictly_inside = all_strictly_inside && (u > 0.0) && (u < 1.0);
        sum += u;
    }
    CHECK(all_strictly_inside);
    CHECK(sum / n == Catch::Approx(0.5).margin(0.01));
}
