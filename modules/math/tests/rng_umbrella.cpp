/// \file
/// The <math/rng.hpp> umbrella pulls in every public symbol of the RNG layer.

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include <math/rng.hpp>

TEST_CASE("<math/rng.hpp> exposes the whole RNG layer", "[rng][umbrella]") {
    using namespace mms::math::rng;

    static_assert(BlockFunction<PhiloxBlock>);
    static_assert(BlockFunction<ThreefryBlock>);

    Counter<4> c{};
    c.bump();
    CHECK(c.words[0] == 1ULL);

    SplitMix64 sm{1};
    CHECK(sm.next() != 0ULL);

    const RngHub hub{Seed{1}};
    RngEngine e = hub.engine_for({.subsystem = Subsystem::Price, .asset = AssetKey{1}});
    RngEngine f = e;
    CHECK(e() == f());

    const CounterBasedEngine<ThreefryBlock, 20> tf{ThreefryBlock::key_type{}};
    CHECK(tf.max() == ~std::uint64_t{0});
}
