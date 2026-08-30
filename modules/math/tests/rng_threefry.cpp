/// \file
/// Known-answer tests for ThreefryBlock (Threefry-4x64).
///
/// Two tiers:
///   1. Hand-derived low-round vectors, computed directly from the Threefry
///      round / key-schedule equations plus the published constants. Each pins
///      one constant or structural property; they run at compile time.
///   2. Full-round (20) vectors from the Random123 project's
///      examples/kat_vectors. TRANSCRIBE / VERIFY these against a fresh copy of
///      that file: a single wrong nibble here silently defeats the test.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include <math/rng/threefry.hpp>

namespace {

using mms::math::rng::ThreefryBlock;
using Ctr = ThreefryBlock::counter_type; // std::array<uint64_t, 4>
using Key = ThreefryBlock::key_type;     // std::array<uint64_t, 4>

constexpr std::uint64_t parity = ThreefryBlock::key_parity;
constexpr std::uint64_t all_ones = ~std::uint64_t{0};

// --- tier 1: hand-derived, verified at compile time --------------------------

static_assert(ThreefryBlock::block(Ctr{1, 2, 3, 4}, Key{0, 0, 0, 0}, 0) == Ctr{1, 2, 3, 4},
              "rounds == 0 with a zero key is the identity");

static_assert(ThreefryBlock::block(Ctr{10, 20, 30, 40}, Key{1, 2, 3, 4}, 0) == Ctr{11, 22, 33, 44},
              "rounds == 0 injects subkey 0, i.e. ctr[i] + key[i]");

static_assert(ThreefryBlock::block(Ctr{1, 0, 0, 0}, Key{0, 0, 0, 0}, 1) == Ctr{1, 1, 0, 0},
              "one even round: x0 += x1 then x1 = rotl(x1, r0) ^ x0");

static_assert(ThreefryBlock::block(Ctr{0, 1, 0, 0}, Key{0, 0, 0, 0}, 1) == Ctr{1, 0x4001, 0, 0},
              "one even round on x1 == 1: rotl(1, 14) ^ 1 == 0x4001, so rot_0[0] == 14");

static_assert(ThreefryBlock::block(Ctr{0, 1, 0, 0}, Key{0, 0, 0, 0}, 2) ==
                  Ctr{1, 0x0200'0000'0000'4081ULL, 0x4001, 1},
              "second (odd) round mixes the (x0,x3) and (x2,x1) pairs and uses "
              "rot_1[1] == 57; a wrong odd-branch structure changes out[1]");

static_assert(ThreefryBlock::block(Ctr{0, 0, 0, 0}, Key{0, 0, 0, 0}, 3) == Ctr{0, 0, 0, 0},
              "three rounds on all-zero input: no subkey injection yet");

static_assert(ThreefryBlock::block(Ctr{0, 0, 0, 0}, Key{0, 0, 0, 0}, 4) == Ctr{0, 0, 0, parity + 1},
              "round 3 injects subkey 1: with a zero key, ks[4] == key_parity "
              "and out[3] gains ks[4] + s with s == 1");

// --- tier 2: full-round KAT from Random123 examples/kat_vectors --------------

void expect(const Ctr& got, const Ctr& want) {
    for (std::size_t i = 0; i < got.size(); ++i) {
        CAPTURE(i, got.at(i), want.at(i));
        CHECK(got.at(i) == want.at(i));
    }
}

} // namespace

TEST_CASE("Threefry4x64 low-round vectors are also verified at run time", "[rng][threefry][kat]") {
    expect(ThreefryBlock::block(Ctr{0, 1, 0, 0}, Key{0, 0, 0, 0}, 2),
           Ctr{1, 0x0200'0000'0000'4081ULL, 0x4001, 1});
    expect(ThreefryBlock::block(Ctr{0, 0, 0, 0}, Key{0, 0, 0, 0}, 4), Ctr{0, 0, 0, parity + 1});
}

TEST_CASE("Threefry4x64-20 published KAT (Random123 kat_vectors)", "[rng][threefry][kat]") {
    // threefry4x64 20 <ctr[0..3]> <key[0..3]> -> <out[0..3]>
    // VERIFY against a fresh copy of Random123 examples/kat_vectors.
    SECTION("all-zero counter and key") {
        expect(ThreefryBlock::block(Ctr{0, 0, 0, 0}, Key{0, 0, 0, 0}, 20),
               Ctr{0x09218ebde6c85537ULL,
                   0x55941f5266d86105ULL,
                   0x4bd25e16282434dcULL,
                   0xee29ec846bd2e40bULL});
    }
    SECTION("all-ones counter and key") {
        expect(ThreefryBlock::block(Ctr{all_ones, all_ones, all_ones, all_ones},
                                    Key{all_ones, all_ones, all_ones, all_ones},
                                    20),
               Ctr{0x29c24097942bba1bULL,
                   0x0371bbfb0f6f4e11ULL,
                   0x3c231ffa33f83a1cULL,
                   0xcd29113fde32d168ULL});
    }
}

TEST_CASE("Threefry4x64-20 is injective in the counter for a fixed key", "[rng][threefry]") {
    constexpr std::size_t n = 256;
    std::set<Ctr> seen;
    for (std::size_t i = 0; i < n; ++i) {
        seen.insert(ThreefryBlock::block(Ctr{i, 0, 0, 0}, Key{0xdead'beefULL, 0, 0, 0}, 20));
    }
    CHECK(seen.size() == n);
}

TEST_CASE("Threefry4x64-20 diffuses a single counter-bit flip", "[rng][threefry]") {
    const Ctr a = ThreefryBlock::block(Ctr{0, 0, 0, 0}, Key{0, 0, 0, 0}, 20);
    const Ctr b = ThreefryBlock::block(Ctr{1, 0, 0, 0}, Key{0, 0, 0, 0}, 20);

    int differing = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        differing += std::popcount(a.at(i) ^ b.at(i));
    }
    CHECK(differing > 96);
    CHECK(differing < 160);
}
