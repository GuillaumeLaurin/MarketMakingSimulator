/// \file
/// Tests for the batch helpers. generate() must equal an operator() loop for
/// every sampler; discard() must leave the engine exactly where k draws would,
/// for every fixed-consumption sampler. The constraints (fixed consumption,
/// jumpable source) are pinned at compile time.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <catch2/catch_test_macros.hpp>

#include <math/dist/discrete.hpp>
#include <math/dist/exponential.hpp>
#include <math/dist/normal.hpp>
#include <math/dist/primitives.hpp>
#include <math/dist/sampler.hpp>
#include <math/dist/traits.hpp>
#include <math/dist/uniform.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/stream.hpp>

#include "word_stub.hpp"

namespace dist = mms::math::dist;
namespace rng = mms::math::rng;

namespace {

using dist::Bernoulli;
using dist::Exponential;
using dist::generate;
using dist::JumpableUint64Source;
using dist::Normal;
using dist::Poisson;
using dist::Sampler;
using dist::StandardNormal;
using dist::UniformInt;
using dist::UniformReal;
using mms::math::testing::WordStub;

rng::RngEngine make_engine(std::uint64_t tag) {
    const rng::RngHub hub{rng::Seed{tag}};
    return hub.engine_for({.subsystem = rng::Subsystem::Price, .asset = rng::AssetKey{1}});
}

template<class S> bool generate_matches_loop(const S& sampler) {
    auto a = make_engine(0x51A7'0001ULL);
    auto b = a;

    constexpr std::size_t n = 48;
    std::array<typename S::result_type, n> via_generate{};
    std::array<typename S::result_type, n> via_loop{};

    generate(sampler, a, std::span<typename S::result_type>{via_generate});
    for (auto& x : via_loop) {
        x = sampler(b);
    }
    return via_generate == via_loop && a == b;
}

template<class S> bool discard_matches_loop(const S& sampler) {
    for (const std::size_t k : {std::size_t{0}, std::size_t{1}, std::size_t{5}, std::size_t{100}}) {
        auto a = make_engine(0xD15C'0002ULL);
        auto b = a;
        dist::discard(sampler, a, k);
        for (std::size_t i = 0; i < k; ++i) {
            (void)sampler(b);
        }
        // discard() and the loop need not leave byte-identical engines (a
        // block-aligned skip does not refill the buffer), but the stream they
        // produce from here on must be the same.
        for (int j = 0; j < 8; ++j) {
            if (a() != b()) {
                return false;
            }
        }
    }
    return true;
}

// A well-formed sampler with variable consumption.
struct VarCost {
    using result_type = double;
    static constexpr int words_per_sample = -1;

    result_type operator()(dist::Uint64Source auto& g) const { return dist::canonical_double(g); }
};

static_assert(Sampler<VarCost>);

template<class S, class G>
concept DiscardCallable = requires(const S& s, G& g) { dist::discard(s, g, std::size_t{1}); };

// discard is defined only for a fixed-consumption sampler ...
static_assert(DiscardCallable<UniformReal, rng::RngEngine>);
static_assert(!DiscardCallable<VarCost, rng::RngEngine>);

// ... and only for a source that can jump.
static_assert(JumpableUint64Source<rng::RngEngine>);
static_assert(!JumpableUint64Source<WordStub>);
static_assert(!DiscardCallable<UniformReal, WordStub>);

} // namespace

TEST_CASE("generate matches an operator() loop for every sampler", "[dist][sampler]") {
    CHECK(generate_matches_loop(UniformReal{-2.0, 3.0}));
    CHECK(generate_matches_loop(UniformInt{-10, 10}));
    CHECK(generate_matches_loop(StandardNormal{}));
    CHECK(generate_matches_loop(Normal{1.0, 2.0}));
    CHECK(generate_matches_loop(Exponential{0.7}));
    CHECK(generate_matches_loop(Bernoulli{0.4}));
    CHECK(generate_matches_loop(Poisson{5.0}));
}

TEST_CASE("discard advances the engine exactly like k draws", "[dist][sampler]") {
    CHECK(discard_matches_loop(UniformReal{0.0, 1.0}));
    CHECK(discard_matches_loop(UniformInt{0, 7}));
    CHECK(discard_matches_loop(StandardNormal{}));
    CHECK(discard_matches_loop(Normal{0.0, 1.0}));
    CHECK(discard_matches_loop(Exponential{1.0}));
    CHECK(discard_matches_loop(Bernoulli{0.5}));
    CHECK(discard_matches_loop(Poisson{3.0}));
}

TEST_CASE("generate on an empty span is a no-op", "[dist][sampler]") {
    auto a = make_engine(0xE0F'0003ULL);
    auto b = a;
    generate(StandardNormal{}, a, std::span<double>{});
    CHECK(a == b);
}
