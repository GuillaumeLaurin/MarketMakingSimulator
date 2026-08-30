/// \file
/// Tests for CounterBasedEngine: block-ordered output, O(1) discard equal to
/// repeated operator(), value-semantic snapshots, seeding, block-aligned
/// position round-trip, and the std::uniform_random_bit_generator contract.
/// Run against both BlockFunction models.

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/rng/counter.hpp>
#include <math/rng/counter_based_engine.hpp>
#include <math/rng/philox.hpp>
#include <math/rng/threefry.hpp>

namespace {

using mms::math::rng::CounterBasedEngine;
using mms::math::rng::PhiloxBlock;
using mms::math::rng::ThreefryBlock;

using Philox = CounterBasedEngine<PhiloxBlock, 10>;
using Threefry = CounterBasedEngine<ThreefryBlock, 20>;

static_assert(std::uniform_random_bit_generator<Philox>);
static_assert(std::uniform_random_bit_generator<Threefry>);
static_assert(Philox::min() == 0ULL);
static_assert(Philox::max() == ~std::uint64_t{0});

// constexpr use: the first four outputs of a fresh engine are exactly the block
// generated at counter 0.
static_assert([] {
    Philox e{Philox::key_type{1, 2}};
    return std::array<std::uint64_t, 4>{e(), e(), e(), e()};
}() == PhiloxBlock::block({0, 0, 0, 0}, {1, 2}, 10));

std::uint64_t drain(auto& engine, int n) {
    std::uint64_t sink = 0;
    for (int i = 0; i < n; ++i) {
        sink ^= engine();
    }
    return sink;
}

} // namespace

TEST_CASE("Philox engine emits the block words in counter order", "[rng][engine]") {
    const Philox::key_type key{0xAAAAULL, 0xBBBBULL};
    Philox e{key};

    const auto b0 = PhiloxBlock::block({0, 0, 0, 0}, key, 10);
    const auto b1 = PhiloxBlock::block({1, 0, 0, 0}, key, 10);
    for (std::size_t i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK(e() == b0.at(i));
    }
    for (std::size_t i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK(e() == b1.at(i));
    }
}

TEST_CASE("Threefry engine emits the block words in counter order", "[rng][engine]") {
    const Threefry::key_type key{1ULL, 2ULL, 3ULL, 4ULL};
    Threefry e{key};

    const auto b0 = ThreefryBlock::block({0, 0, 0, 0}, key, 20);
    for (std::size_t i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK(e() == b0.at(i));
    }
}

TEMPLATE_TEST_CASE("discard(k) equals k calls to operator()", "[rng][engine]", Philox, Threefry) {
    using Engine = TestType;
    const typename Engine::key_type key{};

    for (const unsigned long long k : {0ULL, 1ULL, 3ULL, 4ULL, 7ULL, 8ULL, 9ULL, 100ULL}) {
        Engine a{key};
        Engine b{key};
        a.discard(k);
        std::uint64_t sink = 0;
        for (unsigned long long i = 0; i < k; ++i) {
            sink ^= b();
        }
        CAPTURE(k, sink);
        for (int i = 0; i < 8; ++i) {
            CHECK(a() == b());
        }
    }
}

TEMPLATE_TEST_CASE("a copy is an independent snapshot", "[rng][engine]", Philox, Threefry) {
    using Engine = TestType;
    Engine a{typename Engine::key_type{}};
    CAPTURE(drain(a, 5)); // advance to the middle of the second block

    Engine b = a;
    CHECK(a == b);
    for (int i = 0; i < 20; ++i) {
        CAPTURE(i);
        CHECK(a() == b());
    }
}

TEMPLATE_TEST_CASE("seed makes two engines produce the same stream",
                   "[rng][engine]",
                   Philox,
                   Threefry) {
    using Engine = TestType;
    typename Engine::key_type key{};
    key[0] = 0x5EEDULL;
    typename Engine::counter_type start{};
    start.words[0] = 99ULL;

    Engine a;
    a.seed(key, start);
    Engine b{key, start};

    CHECK(a == b);
    for (int i = 0; i < 32; ++i) {
        CHECK(a() == b());
    }
}

TEMPLATE_TEST_CASE("the same seed reproduces a long sequence", "[rng][engine]", Philox, Threefry) {
    using Engine = TestType;
    typename Engine::key_type key{};
    key[0] = 0xC0FFEEULL;
    Engine a{key};
    Engine b{key};
    for (int i = 0; i < 4096; ++i) {
        CHECK(a() == b());
    }
}

TEMPLATE_TEST_CASE("position() round-trips at a block boundary",
                   "[rng][engine]",
                   Philox,
                   Threefry) {
    using Engine = TestType;
    typename Engine::key_type key{};
    key[0] = 7ULL;

    Engine a{key};
    CAPTURE(drain(a, 4 * 5)); // consume five whole blocks
    const auto pos = a.position();

    Engine b;
    b.seed(key, pos);
    for (int i = 0; i < 16; ++i) {
        CAPTURE(i);
        CHECK(a() == b());
    }
}

TEMPLATE_TEST_CASE("discard is O(1): a huge skip matches split skips and the counter",
                   "[rng][engine]",
                   Philox,
                   Threefry) {
    using Engine = TestType;
    typename Engine::key_type key{};
    key[0] = 3ULL;

    constexpr unsigned long long big = 1ULL << 32;
    Engine a{key};
    a.discard(big);

    Engine b{key};
    b.discard(big / 2);
    b.discard(big / 2);
    CHECK(a == b);

    // block_size == 4 for both models: 2^32 outputs == 2^30 blocks, no leftover.
    CHECK(a.position() == typename Engine::counter_type{{1ULL << 30, 0, 0, 0}});

    for (int i = 0; i < 8; ++i) {
        CHECK(a() == b());
    }
}
