/// \file
/// Tests for AnySampler<T>, the type-erased config-seam wrapper. Every built-in
/// sampler must fit its SBO buffer, wrap without heap, and produce exactly what
/// the naked sampler would on an identical engine. words_per_sample() and
/// discard() must forward correctly, including the replay fallback for the
/// variable-consumption gamma samplers.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include <math/dist/discrete.hpp>
#include <math/dist/exponential.hpp>
#include <math/dist/normal.hpp>
#include <math/dist/sampler.hpp>
#include <math/dist/traits.hpp>
#include <math/dist/uniform.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/stream.hpp>

namespace dist = mms::math::dist;
namespace rng = mms::math::rng;

namespace {

using dist::AnySampler;
using dist::Bernoulli;
using dist::Discrete;
using dist::Exponential;
using dist::Gamma;
using dist::Geometric;
using dist::Normal;
using dist::Poisson;
using dist::Sampler;
using dist::StandardGamma;
using dist::StandardNormal;
using dist::UniformInt;
using dist::UniformReal;

// Every built-in sampler fits the SBO buffer.
static_assert(sizeof(UniformReal) <= AnySampler<double>::buffer_capacity);
static_assert(sizeof(UniformInt) <= AnySampler<std::int64_t>::buffer_capacity);
static_assert(sizeof(StandardNormal) <= AnySampler<double>::buffer_capacity);
static_assert(sizeof(Normal) <= AnySampler<double>::buffer_capacity);
static_assert(sizeof(Exponential) <= AnySampler<double>::buffer_capacity);
static_assert(sizeof(Bernoulli) <= AnySampler<bool>::buffer_capacity);
static_assert(sizeof(Poisson) <= AnySampler<std::int64_t>::buffer_capacity);
static_assert(sizeof(Geometric) <= AnySampler<std::int64_t>::buffer_capacity);
static_assert(sizeof(Discrete) <= AnySampler<std::int64_t>::buffer_capacity);
static_assert(sizeof(StandardGamma) <= AnySampler<double>::buffer_capacity);
static_assert(sizeof(Gamma) <= AnySampler<double>::buffer_capacity);

// AnySampler is not itself a Sampler: words_per_sample is a run-time method.
static_assert(!Sampler<AnySampler<double>>);

rng::RngEngine make_engine(std::uint64_t tag) {
    const rng::RngHub hub{rng::Seed{tag}};
    return hub.engine_for({.subsystem = rng::Subsystem::Price, .asset = rng::AssetKey{21}});
}

template<class T, class S> bool wrap_matches_naked(S sampler) {
    auto a = make_engine(0xA115'0001ULL);
    auto b = a;
    const AnySampler<T> any{sampler};
    bool same = true;
    for (int i = 0; i < 64; ++i) {
        same = same && (any(a) == sampler(b));
    }
    return same;
}

template<class T, class S> bool wrap_discard_matches(S sampler, std::size_t k) {
    auto a = make_engine(0xA115'0002ULL);
    auto b = a;
    const AnySampler<T> any{sampler};
    any.discard(a, k);
    for (std::size_t i = 0; i < k; ++i) {
        (void)sampler(b);
    }
    for (int j = 0; j < 8; ++j) {
        if (a() != b()) {
            return false;
        }
    }
    return true;
}

const std::array<double, 4> discrete_weights{1.0, 2.0, 3.0, 4.0};

} // namespace

TEST_CASE("AnySampler forwards operator() to the wrapped sampler", "[dist][any-sampler]") {
    CHECK(wrap_matches_naked<double>(UniformReal{-2.0, 3.0}));
    CHECK(wrap_matches_naked<double>(StandardNormal{}));
    CHECK(wrap_matches_naked<double>(Normal{1.0, 2.0}));
    CHECK(wrap_matches_naked<double>(Exponential{0.7}));
    CHECK(wrap_matches_naked<double>(StandardGamma{3.0}));
    CHECK(wrap_matches_naked<double>(Gamma{0.6, 1.5}));

    CHECK(wrap_matches_naked<bool>(Bernoulli{0.35}));

    CHECK(wrap_matches_naked<std::int64_t>(UniformInt{-10, 10}));
    CHECK(wrap_matches_naked<std::int64_t>(Poisson{4.0}));
    CHECK(wrap_matches_naked<std::int64_t>(Geometric{0.2}));
    CHECK(wrap_matches_naked<std::int64_t>(Discrete{std::span<const double>{discrete_weights}}));
}

TEST_CASE("AnySampler reports the wrapped sampler's word cost", "[dist][any-sampler]") {
    CHECK(AnySampler<double>{Exponential{1.0}}.words_per_sample() == 1);
    CHECK(AnySampler<double>{Normal{0.0, 1.0}}.words_per_sample() == 1);
    CHECK(AnySampler<std::int64_t>{Poisson{3.0}}.words_per_sample() == 1);
    CHECK(AnySampler<std::int64_t>{Discrete{std::span<const double>{discrete_weights}}}
              .words_per_sample() == 1);
    CHECK(AnySampler<double>{StandardGamma{2.0}}.words_per_sample() == -1);
    CHECK(AnySampler<double>{Gamma{2.0, 1.0}}.words_per_sample() == -1);
}

TEST_CASE("AnySampler::discard stays aligned with naked draws", "[dist][any-sampler]") {
    for (const std::size_t k : {std::size_t{0}, std::size_t{1}, std::size_t{5}, std::size_t{50}}) {
        // fixed-consumption: O(1) engine jump
        CHECK(wrap_discard_matches<double>(Exponential{1.0}, k));
        CHECK(wrap_discard_matches<std::int64_t>(UniformInt{0, 7}, k));
        CHECK(wrap_discard_matches<std::int64_t>(Poisson{3.0}, k));
        // variable-consumption: n-call replay fallback
        CHECK(wrap_discard_matches<double>(StandardGamma{3.0}, k));
        CHECK(wrap_discard_matches<double>(Gamma{0.7, 2.0}, k));
    }
}

TEST_CASE("AnySampler copy is an independent deep copy", "[dist][any-sampler]") {
    auto e1 = make_engine(0xA115'0003ULL);
    auto e2 = e1;

    AnySampler<std::int64_t> copy{Discrete{std::span<const double>{discrete_weights}}};
    {
        const AnySampler<std::int64_t> original{
            Discrete{std::span<const double>{discrete_weights}}};
        copy = original; // copy-assign, then original is destroyed at scope end
    }
    // copy must still work and match a fresh Discrete on the same stream
    const Discrete naked{std::span<const double>{discrete_weights}};
    bool same = true;
    for (int i = 0; i < 64; ++i) {
        same = same && (copy(e1) == naked(e2));
    }
    CHECK(same);
}

TEST_CASE("AnySampler move transfers the wrapped sampler", "[dist][any-sampler]") {
    auto e1 = make_engine(0xA115'0004ULL);
    auto e2 = e1;

    AnySampler<double> src{Gamma{2.5, 1.0}};
    const AnySampler<double> dst{std::move(src)};
    const Gamma naked{2.5, 1.0};

    bool same = true;
    for (int i = 0; i < 32; ++i) {
        same = same && (dst(e1) == naked(e2));
    }
    CHECK(same);
    CHECK(dst.words_per_sample() == -1);
}

TEST_CASE("AnySampler reassignment installs a new sampler", "[dist][any-sampler]") {
    auto engine = make_engine(0xA115'0005ULL);

    AnySampler<double> s{Normal{0.0, 1.0}};
    CHECK(s.words_per_sample() == 1);

    s = AnySampler<double>{Exponential{2.0}}; // move-assign a different sampler
    CHECK(s.words_per_sample() == 1);
    bool positive = true;
    for (int i = 0; i < 1000; ++i) {
        positive = positive && (s(engine) > 0.0);
    }
    CHECK(positive); // exponential is strictly positive; a normal would not be
}

TEST_CASE("generate fills a span through an AnySampler", "[dist][any-sampler]") {
    auto a = make_engine(0xA115'0006ULL);
    auto b = a;
    const AnySampler<double> any{StandardNormal{}};
    const StandardNormal naked;

    std::array<double, 16> via_any{};
    std::array<double, 16> via_loop{};
    dist::generate(any, a, std::span<double>{via_any});
    for (double& x : via_loop) {
        x = naked(b);
    }
    CHECK(via_any == via_loop);
}
