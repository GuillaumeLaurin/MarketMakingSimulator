/// \file
/// Tests for RngHub / StreamKey / Subsystem: deterministic per-stream engines,
/// stream independence, child sub-hubs, and a golden vector that pins the
/// derivation scheme across platforms (Target A reproducibility).

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include <math/rng/stream.hpp>

namespace {

using mms::math::rng::AssetKey;
using mms::math::rng::no_asset;
using mms::math::rng::RngEngine;
using mms::math::rng::RngHub;
using mms::math::rng::Seed;
using mms::math::rng::StreamKey;
using mms::math::rng::Subsystem;

constexpr StreamKey price7{.subsystem = Subsystem::Price, .asset = AssetKey{7}};
constexpr StreamKey price42{.subsystem = Subsystem::Price, .asset = AssetKey{42}};

// GOLDEN VECTOR. Captured from the reference implementation; every platform must
// reproduce it (Target A: the derived stream is bit-identical everywhere). A
// failure here after a deliberate change to the derivation just needs the
// vector regenerated; otherwise it is a real portability regression.
constexpr std::array<std::uint64_t, 4> golden_price42{0x917E'ECAD'A1F9'1E22ULL,
                                                      0x66E5'3386'7A03'14F8ULL,
                                                      0x3299'A5A8'715B'8082ULL,
                                                      0x4199'1DF6'D388'1735ULL};

static_assert([] {
    const RngHub hub{Seed{0xC0FFEEULL}};
    auto e = hub.engine_for(price42);
    return std::array<std::uint64_t, 4>{e(), e(), e(), e()};
}() == golden_price42);

static_assert(price7 == (StreamKey{.subsystem = Subsystem::Price, .asset = AssetKey{7}}));
static_assert(!(price7 == (StreamKey{.subsystem = Subsystem::Latency, .asset = AssetKey{7}})));
static_assert(no_asset == AssetKey{~std::uint64_t{0}});

std::array<std::uint64_t, 8> take8(RngEngine e) {
    std::array<std::uint64_t, 8> out{};
    for (std::uint64_t& v : out) {
        v = e();
    }
    return out;
}

} // namespace

TEST_CASE("RngHub golden vector (runtime check of the compile-time anchor)", "[rng][stream]") {
    const RngHub hub{Seed{0xC0FFEEULL}};
    RngEngine e = hub.engine_for(price42);
    for (std::size_t i = 0; i < golden_price42.size(); ++i) {
        CAPTURE(i);
        CHECK(e() == golden_price42.at(i));
    }
}

TEST_CASE("RngHub is deterministic for a given (master, key)", "[rng][stream]") {
    const RngHub hub{Seed{999}};
    CHECK(hub.engine_for(price7) == hub.engine_for(price7));
    CHECK(take8(hub.engine_for(price7)) == take8(hub.engine_for(price7)));
}

TEST_CASE("two hubs with the same master agree; different masters do not", "[rng][stream]") {
    const RngHub a{Seed{5}};
    const RngHub b{Seed{5}};
    const RngHub c{Seed{6}};
    CHECK(a.engine_for(price7) == b.engine_for(price7));
    CHECK(a.engine_for(price7) != c.engine_for(price7));
}

TEST_CASE("distinct StreamKeys give distinct, non-overlapping streams", "[rng][stream]") {
    const RngHub hub{Seed{0x1234}};
    const std::array<StreamKey, 4> keys{
        StreamKey{.subsystem = Subsystem::Price, .asset = AssetKey{7}},
        StreamKey{.subsystem = Subsystem::Latency, .asset = AssetKey{7}},
        StreamKey{.subsystem = Subsystem::Price, .asset = AssetKey{8}},
        StreamKey{.subsystem = Subsystem::PriceFactor, .asset = no_asset},
    };
    std::set<std::array<std::uint64_t, 8>> seen;
    for (const StreamKey& k : keys) {
        seen.insert(take8(hub.engine_for(k)));
    }
    CHECK(seen.size() == keys.size());
}

TEST_CASE("child sub-hubs are independent of the parent and each other", "[rng][stream]") {
    const RngHub hub{Seed{77}};
    const RngHub k3 = hub.child(3);
    const RngHub k4 = hub.child(4);
    CHECK(k3.engine_for(price7) != hub.engine_for(price7));
    CHECK(k3.engine_for(price7) != k4.engine_for(price7));
    CHECK(k3.engine_for(price7) == hub.child(3).engine_for(price7)); // deterministic
}
