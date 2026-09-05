/// \file
/// Tests for Counter<N>: increment-with-carry, wraparound, comparison, indexing.

#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include <math/rng/counter.hpp>

namespace {

using mms::math::rng::Counter;
using C2 = Counter<2>;
using W2 = std::array<std::uint64_t, 2>;

constexpr std::uint64_t all_ones = ~std::uint64_t{0};

constexpr W2 after_bump(C2 c, std::uint64_t add) {
    c.bump(add);
    return c.words;
}

static_assert(after_bump(C2{}, 1) == W2{1, 0});
static_assert(after_bump(C2{{all_ones, 0}}, 1) == W2{0, 1}, "carry out of word 0");
static_assert(after_bump(C2{{all_ones, all_ones}}, 1) == W2{0, 0},
              "overflow of the whole counter wraps silently");
static_assert(after_bump(C2{{5, 0}}, 3) == W2{8, 0});
static_assert(after_bump(C2{{all_ones - 2, 0}}, 5) == W2{2, 1}, "bump(k > 1) carries");
static_assert(after_bump(C2{{0, 0}}, all_ones) == W2{all_ones, 0});

static_assert(C2{{1, 2}} == C2{{1, 2}});
static_assert(!(C2{{1, 2}} == C2{{1, 3}}));
static_assert(C2{{7, 9}}[0] == 7 && C2{{7, 9}}[1] == 9);

} // namespace

TEST_CASE("Counter::bump increments the little-endian value", "[rng][counter]") {
    C2 c{};
    c.bump();
    CHECK(c.words == W2{1, 0});
}

TEST_CASE("Counter::bump carries between words", "[rng][counter]") {
    C2 c{{all_ones, 0}};
    c.bump();
    CHECK(c.words == W2{0, 1});
}

TEST_CASE("Counter::bump(k) equals k single bumps", "[rng][counter]") {
    C2 a{{0xFFFF'0000ULL, 3}};
    C2 b = a;
    constexpr std::uint64_t k = 1'000'003;
    a.bump(k);
    for (std::uint64_t i = 0; i < k; ++i) {
        b.bump();
    }
    CHECK(a.words == b.words);
}

TEST_CASE("Counter::bump(k) crossing a word boundary", "[rng][counter]") {
    C2 c{{all_ones - 2, 0}};
    c.bump(5);
    CHECK(c.words == W2{2, 1});
}

TEST_CASE("Counter comparison and indexing", "[rng][counter]") {
    CHECK(Counter<4>{{1, 2, 3, 4}} == Counter<4>{{1, 2, 3, 4}});
    CHECK(Counter<4>{{1, 2, 3, 4}} != Counter<4>{{1, 2, 3, 5}});
    const Counter<4> c{{10, 20, 30, 40}};
    CHECK(c[2] == 30);
}
