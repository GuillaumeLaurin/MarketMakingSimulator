/// \file
/// Tests for RngHub / StreamKey / Subsystem: deterministic per-stream engines,
/// stream independence, child sub-hubs, and a golden vector that pins the
/// derivation scheme across platforms (Target A reproducibility).

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <math/rng/stream.hpp>

namespace {

using mms::math::rng::asset_key_of;
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

// FNV-1a 64 as published (the offset basis for the empty string, and the usual test
// strings). Computed independently of the implementation under test.
static_assert(asset_key_of("") == AssetKey{0xCBF29CE484222325ULL});
static_assert(asset_key_of("a") == AssetKey{0xAF63DC4C8601EC8CULL});
static_assert(asset_key_of("foo") == AssetKey{0xDCB27518FED9D577ULL});
static_assert(asset_key_of("foobar") == AssetKey{0x85944171F73967E8ULL});

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
    const std::array<StreamKey, 6> keys{
        StreamKey{.subsystem = Subsystem::Price, .asset = AssetKey{7}},
        StreamKey{.subsystem = Subsystem::Latency, .asset = AssetKey{7}},
        StreamKey{.subsystem = Subsystem::Price, .asset = AssetKey{8}},
        StreamKey{.subsystem = Subsystem::PriceFactor, .asset = no_asset},
        StreamKey{.subsystem = Subsystem::Agent, .asset = AssetKey{7}},
        StreamKey{.subsystem = Subsystem::AgentLatency, .asset = AssetKey{7}},
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

// ------------------------------------------------------------ asset_key_of

TEST_CASE("asset_key_of is FNV-1a 64 over the bytes of the symbol", "[rng][stream][asset_key]") {
    // The same vectors as the compile-time anchors above, plus the symbols the
    // simulation actually uses, worked out independently of the header.
    CHECK(asset_key_of("") == AssetKey{0xCBF29CE484222325ULL});
    CHECK(asset_key_of("a") == AssetKey{0xAF63DC4C8601EC8CULL});
    CHECK(asset_key_of("foo") == AssetKey{0xDCB27518FED9D577ULL});
    CHECK(asset_key_of("foobar") == AssetKey{0x85944171F73967E8ULL});
    CHECK(asset_key_of("AAPL") == AssetKey{0x89106B8B9F086CCBULL});
    CHECK(asset_key_of("aapl") == AssetKey{0xF4EE6A83EA65D80BULL});
}

TEST_CASE("asset_key_of treats every byte as unsigned", "[rng][stream][asset_key]") {
    // A signed char would sign-extend 0xFF to all ones before the xor, which is
    // a different key on a platform where char is signed.
    const std::string_view one_byte{"\xFF", 1};
    CHECK(asset_key_of(one_byte) == AssetKey{0xAF64724C8602EB6EULL});
}

TEST_CASE("asset_key_of reads the whole view, not up to a NUL", "[rng][stream][asset_key]") {
    const std::string_view with_nul{"A\0B", 3};
    CHECK(asset_key_of(with_nul) != asset_key_of("A"));
    CHECK(asset_key_of(with_nul) != asset_key_of("AB"));
}

TEST_CASE("asset_key_of is case sensitive and order sensitive", "[rng][stream][asset_key]") {
    CHECK(asset_key_of("AAPL") != asset_key_of("aapl"));
    CHECK(asset_key_of("AB") != asset_key_of("BA"));
    CHECK(asset_key_of("A") != asset_key_of("AA"));
}

TEST_CASE("asset_key_of gives distinct keys to a realistic list of symbols",
          "[rng][stream][asset_key]") {
    const std::array<std::string_view, 24> symbols{
        "AAPL", "MSFT", "GOOG", "AMZN", "TSLA",   "META",   "NVDA",   "NFLX",
        "JPM",  "BAC",  "WFC",  "GS",   "XOM",    "CVX",    "KO",     "PEP",
        "DIS",  "BA",   "T",    "VZ",   "ASSET0", "ASSET1", "ASSET2", "ASSET3"};
    std::set<std::uint64_t> seen;
    for (const std::string_view symbol : symbols) {
        const auto key = static_cast<std::uint64_t>(asset_key_of(symbol));
        CHECK(key != static_cast<std::uint64_t>(no_asset));
        seen.insert(key);
    }
    CHECK(seen.size() == symbols.size());
}

TEST_CASE("An asset's stream follows its symbol, not its position", "[rng][stream][asset_key]") {
    const RngHub hub{Seed{0xABCD}};
    const StreamKey aapl{.subsystem = Subsystem::Price, .asset = asset_key_of("AAPL")};
    const StreamKey msft{.subsystem = Subsystem::Price, .asset = asset_key_of("MSFT")};

    // The same symbol is the same stream, whatever else exists or was asked for.
    const auto before = take8(hub.engine_for(aapl));
    static_cast<void>(hub.engine_for(msft));
    static_cast<void>(
        hub.engine_for(StreamKey{.subsystem = Subsystem::Price, .asset = asset_key_of("NEWCO")}));
    CHECK(take8(hub.engine_for(aapl)) == before);

    // Two symbols are two streams, and so are two subsystems of one symbol.
    CHECK(hub.engine_for(aapl) != hub.engine_for(msft));
    CHECK(hub.engine_for(aapl) != hub.engine_for(StreamKey{.subsystem = Subsystem::Latency,
                                                           .asset = asset_key_of("AAPL")}));
}

TEST_CASE("An agent's stream follows its name and is apart from every asset's",
          "[rng][stream][asset_key]") {
    const RngHub hub{Seed{0xFACE}};
    const StreamKey maker{.subsystem = Subsystem::Agent, .asset = asset_key_of("maker")};
    const StreamKey noise{.subsystem = Subsystem::Agent, .asset = asset_key_of("noise-1")};

    // Another agent coming or going leaves this one's stream alone.
    const auto before = take8(hub.engine_for(maker));
    static_cast<void>(hub.engine_for(noise));
    CHECK(take8(hub.engine_for(maker)) == before);
    CHECK(hub.engine_for(maker) != hub.engine_for(noise));

    // An agent called AAPL does not share a stream with the asset AAPL, for any of
    // the asset's subsystems.
    const auto aapl = asset_key_of("AAPL");
    const StreamKey agent{.subsystem = Subsystem::Agent, .asset = aapl};
    const StreamKey agent_latency{.subsystem = Subsystem::AgentLatency, .asset = aapl};
    for (const Subsystem subsystem :
         {Subsystem::Price, Subsystem::AdverseFlow, Subsystem::Latency, Subsystem::PriceFactor}) {
        const StreamKey asset_stream{.subsystem = subsystem, .asset = aapl};
        CHECK(hub.engine_for(agent) != hub.engine_for(asset_stream));
        CHECK(hub.engine_for(agent_latency) != hub.engine_for(asset_stream));
    }
    // And an agent's decisions and the delays of its messages are two streams.
    CHECK(hub.engine_for(agent) != hub.engine_for(agent_latency));
}

TEST_CASE("Subsystem values are stable, since they are mixed into every stream", "[rng][stream]") {
    STATIC_REQUIRE(static_cast<int>(Subsystem::Price) == 0);
    STATIC_REQUIRE(static_cast<int>(Subsystem::AdverseFlow) == 1);
    STATIC_REQUIRE(static_cast<int>(Subsystem::Latency) == 2);
    STATIC_REQUIRE(static_cast<int>(Subsystem::PriceFactor) == 3);
    STATIC_REQUIRE(static_cast<int>(Subsystem::Agent) == 4);
    STATIC_REQUIRE(static_cast<int>(Subsystem::AgentLatency) == 5);
}
