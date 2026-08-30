/// \file
/// Tests for mms::math::mulhi64 (the high half of a 64x64 -> 128 unsigned
/// multiply). On GCC / Clang every call takes the __uint128_t path; on MSVC the
/// static_asserts exercise the portable 32x32 decomposition while the run-time
/// checks exercise the __umulh intrinsic.

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include <math/bits.hpp>

namespace {

using mms::math::mulhi64;

constexpr std::uint64_t k62 = std::uint64_t{1} << 62;
constexpr std::uint64_t k63 = std::uint64_t{1} << 63;
constexpr std::uint64_t all_ones = ~std::uint64_t{0};

static_assert(mulhi64(0ULL, 12345ULL) == 0ULL);
static_assert(mulhi64(12345ULL, 0ULL) == 0ULL);
static_assert(mulhi64(1ULL, all_ones) == 0ULL, "the product still fits in 64 bits");
static_assert(mulhi64(all_ones, 1ULL) == 0ULL);
static_assert(mulhi64(k63, 2ULL) == 1ULL, "2^63 * 2 == 2^64");
static_assert(mulhi64(k63, k63) == k62, "2^126 >> 64 == 2^62");
static_assert(mulhi64(all_ones, 2ULL) == 1ULL, "(2^64-1) * 2 == 2^65 - 2");
static_assert(mulhi64(all_ones, all_ones) == all_ones - 1ULL, "(2^64-1)^2 >> 64 == 2^64 - 2");

#ifdef __SIZEOF_INT128__
__extension__ using u128 = unsigned __int128;

constexpr std::uint64_t ref_mulhi(std::uint64_t a, std::uint64_t b) {
    return static_cast<std::uint64_t>((static_cast<u128>(a) * b) >> 64);
}
#endif

} // namespace

TEST_CASE("mulhi64 known values (also checked at compile time)", "[math][bits]") {
    CHECK(mulhi64(0ULL, 12345ULL) == 0ULL);
    CHECK(mulhi64(k63, 2ULL) == 1ULL);
    CHECK(mulhi64(k63, k63) == k62);
    CHECK(mulhi64(all_ones, all_ones) == all_ones - 1ULL);
    CHECK(mulhi64(0xDEAD'BEEF'CAFE'BABEULL, 0x0123'4567'89AB'CDEFULL) ==
          mulhi64(0x0123'4567'89AB'CDEFULL, 0xDEAD'BEEF'CAFE'BABEULL));
}

#ifdef __SIZEOF_INT128__
TEST_CASE("mulhi64 matches a 128-bit reference over a pseudo-random sample", "[math][bits]") {
    std::uint64_t s = 0x1234'5678'9ABC'DEF0ULL;
    auto next = [&s] {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    };
    for (int i = 0; i < 20'000; ++i) {
        const std::uint64_t a = next();
        const std::uint64_t b = next();
        CAPTURE(i, a, b);
        CHECK(mulhi64(a, b) == ref_mulhi(a, b));
    }
}
#endif
