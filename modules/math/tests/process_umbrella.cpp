/// \file
/// The <math/process.hpp> umbrella pulls in every public symbol of the
/// process layer.

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <math/process.hpp>
#include <math/rng/stream.hpp>

namespace {

std::vector<mms::math::rng::RngEngine>
engines_for(const mms::math::rng::RngHub& hub, mms::math::rng::Subsystem subsystem, int n) {
    std::vector<mms::math::rng::RngEngine> engines;
    for (int i = 0; i < n; ++i) {
        engines.push_back(
            hub.engine_for({.subsystem = subsystem,
                            .asset = mms::math::rng::AssetKey{static_cast<std::uint64_t>(i)}}));
    }
    return engines;
}

} // namespace

TEST_CASE("<math/process.hpp> exposes the whole process layer", "[process][umbrella]") {
    using namespace mms::math::process;
    namespace rng = mms::math::rng;

    const rng::RngHub hub{rng::Seed{0x9999'0000ULL}};

    // increment.hpp
    mms::math::Matrix chol = mms::math::Matrix::Identity(2, 2);
    std::vector<rng::RngEngine> increment_engines = engines_for(hub, rng::Subsystem::Price, 2);
    const mms::math::Vector increment = correlated_normal_increment(chol, increment_engines, 1.0);
    CHECK(increment.size() == 2);

    // correlated_random_walk.hpp, accessed through the PriceProcess base
    // (price_process.hpp) to exercise the NVI dt >= 0 contract polymorphically.
    mms::math::Vector prices(2);
    prices << 100.0, 50.0;
    mms::math::Vector mu = mms::math::Vector::Zero(2);
    const std::unique_ptr<PriceProcess> walk = std::make_unique<CorrelatedRandomWalk>(
        prices, mu, chol, engines_for(hub, rng::Subsystem::Price, 2));

    CHECK(walk->asset_count() == 2);
    walk->advance(1.0);
    CHECK_THROWS_AS(walk->advance(-1.0), std::invalid_argument);
    walk->reset();
    CHECK(walk->prices()(0) == prices(0));

    // factor_model.hpp, also through the PriceProcess base.
    mms::math::Matrix beta = mms::math::Matrix::Zero(2, 1);
    mms::math::Matrix factor_chol(1, 1);
    factor_chol << 1.0;
    mms::math::Vector idio_vol = mms::math::Vector::Zero(2);
    const std::unique_ptr<PriceProcess> factor_model =
        std::make_unique<FactorModel>(prices,
                                      mu,
                                      beta,
                                      factor_chol,
                                      idio_vol,
                                      engines_for(hub, rng::Subsystem::Price, 2),
                                      engines_for(hub, rng::Subsystem::PriceFactor, 1));

    CHECK(factor_model->asset_count() == 2);
    factor_model->advance(1.0);
    factor_model->reset();
    CHECK(factor_model->prices()(1) == prices(1));
}
