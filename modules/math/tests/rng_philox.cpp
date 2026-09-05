/// \file
/// Known-answer tests for PhiloxBlock (Philox-4x64).
///
/// Two tiers:
///   1. Hand-derived low-round vectors. Each is computed directly from the
///      Philox round equations plus the published constants, so it pins one
///      specific constant or structural property and needs no external file.
///      These run at compile time via static_assert.
///   2. Full-round (10) vectors from the Random123 project's
///      examples/kat_vectors. TRANSCRIBE / VERIFY these against a fresh copy of
///      that file: a single wrong nibble here silently defeats the test.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include <math/rng/philox.hpp>

namespace {

using mms::math::rng::PhiloxBlock;
using Ctr = PhiloxBlock::counter_type; // std::array<uint64_t, 4>
using Key = PhiloxBlock::key_type;     // std::array<uint64_t, 2>

constexpr std::uint64_t m0 = PhiloxBlock::multiplier_0;
constexpr std::uint64_t m1 = PhiloxBlock::multiplier_1;
constexpr std::uint64_t w0 = PhiloxBlock::weyl_0;
constexpr std::uint64_t w1 = PhiloxBlock::weyl_1;
constexpr std::uint64_t all_ones = ~std::uint64_t{0};

// --- tier 1: hand-derived, verified at compile time --------------------------

static_assert(PhiloxBlock::block(Ctr{1, 2, 3, 4}, Key{5, 6}, 0) == Ctr{1, 2, 3, 4},
              "rounds == 0 is the identity (the round loop never runs)");

static_assert(PhiloxBlock::block(Ctr{0, 0, 0, 0}, Key{0, 0}, 1) == Ctr{0, 0, 0, 0},
              "one round on all-zero input stays all-zero");

static_assert(PhiloxBlock::block(Ctr{1, 0, 0, 0}, Key{0, 0}, 1) == Ctr{0, 0, 0, m0},
              "one round, ctr[0] == 1: lo(multiplier_0 * 1) lands in out[3]");

static_assert(PhiloxBlock::block(Ctr{0, 0, 1, 0}, Key{0, 0}, 1) == Ctr{0, m1, 0, 0},
              "one round, ctr[2] == 1: lo(multiplier_1 * 1) lands in out[1]");

static_assert(PhiloxBlock::block(Ctr{0, 0, 0, 0}, Key{0, 0}, 2) == Ctr{w0, 0, w1, 0},
              "two rounds: the key is bumped by (weyl_0, weyl_1) before round 1 "
              "only, never before round 0");

// --- tier 2: full-round KAT from Random123 examples/kat_vectors --------------

void expect(const Ctr& got, const Ctr& want) {
    for (std::size_t i = 0; i < got.size(); ++i) {
        CAPTURE(i, got.at(i), want.at(i));
        CHECK(got.at(i) == want.at(i));
    }
}

} // namespace

TEST_CASE("Philox4x64 low-round vectors are also verified at run time", "[rng][philox][kat]") {
    expect(PhiloxBlock::block(Ctr{0, 0, 0, 0}, Key{0, 0}, 2), Ctr{w0, 0, w1, 0});
    expect(PhiloxBlock::block(Ctr{1, 0, 0, 0}, Key{0, 0}, 1), Ctr{0, 0, 0, m0});
}

TEST_CASE("Philox4x64-10 published KAT (Random123 kat_vectors)", "[rng][philox][kat]") {
    // philox4x64 10 <ctr[0..3]> <key[0..1]> -> <out[0..3]>
    // VERIFY against a fresh copy of Random123 examples/kat_vectors.
    SECTION("all-zero counter and key") {
        expect(PhiloxBlock::block(Ctr{0, 0, 0, 0}, Key{0, 0}, 10),
               Ctr{0x16554d9eca36314cULL,
                   0xdb20fe9d672d0fdcULL,
                   0xd7e772cee186176bULL,
                   0x7e68b68aec7ba23bULL});
    }
    SECTION("all-ones counter and key") {
        expect(PhiloxBlock::block(
                   Ctr{all_ones, all_ones, all_ones, all_ones}, Key{all_ones, all_ones}, 10),
               Ctr{0x87b092c3013fe90bULL,
                   0x438c3c67be8d0224ULL,
                   0x9cc7d7c69cd777b6ULL,
                   0xa09caebf594f0ba0ULL});
    }
}

TEST_CASE("Philox4x64-10 is injective in the counter for a fixed key", "[rng][philox]") {
    constexpr std::size_t n = 256;
    std::set<Ctr> seen;
    for (std::size_t i = 0; i < n; ++i) {
        seen.insert(PhiloxBlock::block(Ctr{i, 0, 0, 0}, Key{0xdead'beefULL, 0}, 10));
    }
    CHECK(seen.size() == n);
}

TEST_CASE("Philox4x64-10 diffuses a single counter-bit flip", "[rng][philox]") {
    const Ctr a = PhiloxBlock::block(Ctr{0, 0, 0, 0}, Key{0, 0}, 10);
    const Ctr b = PhiloxBlock::block(Ctr{1, 0, 0, 0}, Key{0, 0}, 10);

    int differing = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        differing += std::popcount(a.at(i) ^ b.at(i));
    }
    // A correct 10-round Philox flips close to half of the 256 output bits;
    // an under-mixed or identity implementation flips far fewer.
    CHECK(differing > 96);
    CHECK(differing < 160);
}
