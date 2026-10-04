/// \file
/// Tests for simulation/asset_spec.hpp: field defaults and exactness,
/// FactorModelAssetData only present when built, and that AssetSpec reuses
/// size_spec's DistributionSpec rather than redefining its own.

#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <mm/types.hpp>

#include <math/types.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/size_spec.hpp>

namespace sim = mms::simulation;
namespace mm = mms::mm;
namespace math = mms::math;

TEST_CASE("AssetSpec's optional fields default to zero or empty", "[simulation][asset_spec]") {
    const sim::AssetSpec spec{.symbol = "TEST",
                              .initial_mid = 100.0,
                              .price_volatility = 0.2,
                              .factor_model = std::nullopt,
                              .adverse_flow_size = sim::ExponentialSpec{.mean = 5.0}};

    CHECK(spec.maker_fee_bps == 0.0);
    CHECK(spec.mu == 0.0);
    CHECK_FALSE(spec.factor_model.has_value());
    CHECK(spec.adverse_flow_lambda == 0.0);
    CHECK(spec.adverse_flow_direction_correlation == 0.0); // pure noise unless asked otherwise
}

TEST_CASE("AssetSpec holds every field exactly when all are set", "[simulation][asset_spec]") {
    math::Vector beta(2);
    beta << 0.5, -0.3;

    const sim::AssetSpec spec{
        .symbol = "HOLD",
        .initial_mid = 50.0,
        .maker_fee_bps = 1.5,
        .price_volatility = 0.3,
        .mu = 0.01,
        .factor_model = sim::FactorModelAssetData{.beta = beta, .idiosyncratic_volatility = 0.1},
        .adverse_flow_lambda = 2.0,
        .adverse_flow_direction_correlation = 0.75,
        .adverse_flow_size = sim::UniformIntSpec{.min = 1, .max = 20}};

    CHECK(spec.symbol == "HOLD");
    CHECK(spec.initial_mid == 50.0);
    CHECK(spec.maker_fee_bps == 1.5);
    CHECK(spec.price_volatility == 0.3);
    CHECK(spec.mu == 0.01);
    REQUIRE(spec.factor_model.has_value());
    CHECK(spec.factor_model->beta(0) == 0.5);
    CHECK(spec.factor_model->beta(1) == -0.3);
    CHECK(spec.factor_model->idiosyncratic_volatility == 0.1);
    CHECK(spec.adverse_flow_lambda == 2.0);
    CHECK(spec.adverse_flow_direction_correlation == 0.75);
    REQUIRE(std::holds_alternative<sim::UniformIntSpec>(spec.adverse_flow_size));
    CHECK(std::get<sim::UniformIntSpec>(spec.adverse_flow_size).max == 20);
}

TEST_CASE("A RandomWalk-style AssetSpec simply leaves factor_model empty",
          "[simulation][asset_spec]") {
    const sim::AssetSpec spec{.symbol = "TEST",
                              .initial_mid = 100.0,
                              .price_volatility = 0.2,
                              .factor_model = std::nullopt,
                              .adverse_flow_size = sim::ExponentialSpec{.mean = 3.0}};

    CHECK_FALSE(spec.factor_model.has_value());
}

TEST_CASE("initial_mid is mm::Price and price-related rates are plain double",
          "[simulation][asset_spec]") {
    STATIC_REQUIRE(std::is_same_v<decltype(sim::AssetSpec::symbol), std::string>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::AssetSpec::initial_mid), mm::Price>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::AssetSpec::price_volatility), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::AssetSpec::mu), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::AssetSpec::adverse_flow_lambda), double>);
    STATIC_REQUIRE(
        std::is_same_v<decltype(sim::AssetSpec::adverse_flow_direction_correlation), double>);
}

TEST_CASE("AssetSpec::adverse_flow_size is exactly size_spec's DistributionSpec",
          "[simulation][asset_spec]") {
    STATIC_REQUIRE(
        std::is_same_v<decltype(sim::AssetSpec::adverse_flow_size), sim::DistributionSpec>);
}

TEST_CASE("FactorModelAssetData holds its beta vector and idiosyncratic volatility exactly",
          "[simulation][asset_spec]") {
    math::Vector beta(3);
    beta << 1.0, 2.0, 3.0;

    const sim::FactorModelAssetData data{.beta = beta, .idiosyncratic_volatility = 0.15};

    REQUIRE(data.beta.size() == 3);
    CHECK(data.beta(2) == 3.0);
    CHECK(data.idiosyncratic_volatility == 0.15);
}
