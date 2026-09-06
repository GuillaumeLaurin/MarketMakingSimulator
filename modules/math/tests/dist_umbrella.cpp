/// \file
/// The <math/dist.hpp> umbrella pulls in every public symbol of the dist layer.

#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include <catch2/catch_test_macros.hpp>

#include <math/dist.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/stream.hpp>

TEST_CASE("<math/dist.hpp> exposes the whole dist layer", "[dist][umbrella]") {
    using namespace mms::math::dist;

    // primitives.hpp + traits.hpp + sampler.hpp concepts
    static_assert(Uint64Source<mms::math::rng::RngEngine>);
    static_assert(JumpableUint64Source<mms::math::rng::RngEngine>);
    static_assert(Sampler<StandardNormal>);

    const mms::math::rng::RngHub hub{mms::math::rng::Seed{42}};
    mms::math::rng::RngEngine engine = hub.engine_for(
        {.subsystem = mms::math::rng::Subsystem::Price, .asset = mms::math::rng::AssetKey{1}});

    // primitives.hpp
    CHECK(canonical_double(engine) >= 0.0);
    CHECK(open_unit_double(engine) > 0.0);
    CHECK(uniform_bits(engine) != uniform_bits(engine));

    // normal.hpp free helper
    CHECK(probit(0.5) == 0.0);

    // one draw from every sampler type
    CHECK(UniformReal{0.0, 1.0}(engine) >= 0.0);
    CHECK(UniformInt{1, 6}(engine) >= 1);
    CHECK(std::isfinite(StandardNormal{}(engine)));
    CHECK(std::isfinite(Normal{0.0, 1.0}(engine)));
    CHECK(Exponential{1.0}(engine) > 0.0);
    (void)Bernoulli{0.5}(engine);
    CHECK(Poisson{3.0}(engine) >= 0);

    // sampler.hpp batch helpers
    std::array<double, 4> out{};
    generate(StandardNormal{}, engine, std::span<double>{out});
    discard(StandardNormal{}, engine, 3);
    CHECK(std::isfinite(out[0]));

    // free-function forms
    CHECK(std::isfinite(standard_normal(engine)));
    CHECK(std::isfinite(normal(engine, 1.0, 2.0)));
    CHECK(uniform_real(engine, -1.0, 1.0) >= -1.0);
    CHECK(uniform_int(engine, 0, 10) >= 0);
    CHECK(exponential(engine, 2.0) > 0.0);
    (void)bernoulli(engine, 0.5);
    CHECK(poisson(engine, 4.0) >= 0);
}
