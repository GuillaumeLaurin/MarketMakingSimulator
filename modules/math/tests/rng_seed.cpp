/// \file
/// Tests for Seed / SplitMix64 / spawn.

#include <array>
#include <cstdint>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include <math/rng/seed.hpp>

namespace {

using mms::math::rng::Seed;
using mms::math::rng::spawn;
using mms::math::rng::SplitMix64;
using mms::math::rng::to_bits;

// Canonical SplitMix64 output for seed 0 (Vigna's reference splitmix64.c).
constexpr std::array<std::uint64_t, 5> splitmix_seed0{0xE220'A839'7B1D'CDAFULL,
                                                      0x6E78'9E6A'A1B9'65F4ULL,
                                                      0x06C4'5D18'8009'454FULL,
                                                      0xF88B'B8A8'724C'81ECULL,
                                                      0x1B39'896A'51A8'749BULL};

constexpr std::uint64_t first_of(std::uint64_t seed) {
    SplitMix64 s{seed};
    return s.next();
}

static_assert(first_of(0) == splitmix_seed0[0]);
static_assert(to_bits(static_cast<Seed>(0xABCDULL)) == 0xABCDULL, "Seed round-trips its bits");
static_assert(to_bits(spawn(Seed{7}, 3)) == to_bits(spawn(Seed{7}, 3)), "spawn is a pure function");
static_assert(to_bits(spawn(Seed{7}, 3)) != to_bits(spawn(Seed{7}, 4)));
static_assert(to_bits(spawn(Seed{7}, 3)) != to_bits(spawn(Seed{8}, 3)));

} // namespace

TEST_CASE("SplitMix64 matches the canonical vectors for seed 0", "[rng][seed]") {
    SplitMix64 s{0};
    for (const std::uint64_t want : splitmix_seed0) {
        CHECK(s.next() == want);
    }
}

TEST_CASE("SplitMix64 is deterministic", "[rng][seed]") {
    SplitMix64 a{0x1234'5678ULL};
    SplitMix64 b{0x1234'5678ULL};
    for (int i = 0; i < 64; ++i) {
        CHECK(a.next() == b.next());
    }
}

TEST_CASE("spawn gives distinct child seeds", "[rng][seed]") {
    std::set<std::uint64_t> seen;
    for (std::uint64_t i = 0; i < 512; ++i) {
        seen.insert(to_bits(spawn(Seed{0xDEAD'BEEFULL}, i)));
    }
    CHECK(seen.size() == 512);
}
