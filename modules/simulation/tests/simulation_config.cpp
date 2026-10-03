/// \file
/// Tests for simulation/simulation_config.hpp: a plain aggregate assembling
/// every piece this zone has built so far, so this mostly pins field
/// exactness and types, plus the one field the SIM6/SIM18 correction added
/// (`process`, a whole-simulation choice, not per asset).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mm/strategy_config.hpp>

#include <math/rng/seed.hpp>
#include <math/types.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/latency_config.hpp>
#include <simulation/price_process_spec.hpp>
#include <simulation/simulation_config.hpp>

namespace sim = mms::simulation;
namespace math = mms::math;
namespace mm = mms::mm;

namespace {

sim::AssetSpec minimal_asset(mm::Price mid) {
    return sim::AssetSpec{.initial_mid = mid,
                          .price_volatility = 0.2,
                          .factor_model = std::nullopt,
                          .adverse_flow_size = sim::ExponentialSpec{.mean = 5.0}};
}

mm::StrategyConfig minimal_strategy(std::size_t assets) {
    return mm::StrategyConfig{
        .gamma = 2.0,
        .k = math::Vector::Constant(static_cast<Eigen::Index>(assets), 1.0),
        .correlation = math::Matrix::Identity(static_cast<Eigen::Index>(assets),
                                              static_cast<Eigen::Index>(assets)),
        .estimators = std::vector<mm::EstimatorSpec>(assets, mm::ConstantSpec{.sigma = 0.2}),
        .horizon = mm::ConstantHorizon{.tau = 1.0},
        .quote_sizes = std::vector<mm::Quantity>(assets, 10),
        .tick_sizes = std::vector<mm::Price>(assets, 0.01),
        .limits = {.max_abs_position = std::vector<mm::Quantity>(assets, 1000),
                   .max_portfolio_risk = std::nullopt},
        .min_samples = 0};
}

} // namespace

TEST_CASE("SimulationConfig holds every top-level field exactly",
          "[simulation][simulation_config]") {
    const sim::SimulationConfig config{
        .assets = {minimal_asset(100.0), minimal_asset(50.0)},
        .correlation = math::Matrix::Identity(2, 2),
        .process = sim::RandomWalkSpec{},
        .strategy = minimal_strategy(2),
        .latency = {.quote_delay = sim::ConstantDelaySpec{.delay = 0.0},
                    .adverse_flow_delay = sim::ConstantDelaySpec{.delay = 0.0}},
        .master_seed = math::rng::Seed{42},
        .dt = 0.5,
        .tick_count = 1000};

    REQUIRE(config.assets.size() == 2);
    CHECK(config.assets[0].initial_mid == 100.0);
    CHECK(config.assets[1].initial_mid == 50.0);
    CHECK(config.correlation.rows() == 2);
    CHECK(config.strategy.quote_sizes.size() == 2);
    CHECK(config.master_seed == math::rng::Seed{42});
    CHECK(config.dt == 0.5);
    CHECK(config.tick_count == 1000);
}

TEST_CASE("SimulationConfig::process is a whole-simulation choice, holding whichever "
          "PriceProcessSpec alternative it was built from",
          "[simulation][simulation_config]") {
    sim::SimulationConfig config{
        .assets = {minimal_asset(100.0)},
        .correlation = math::Matrix::Identity(1, 1),
        .process = sim::RandomWalkSpec{},
        .strategy = minimal_strategy(1),
        .latency = {.quote_delay = sim::ConstantDelaySpec{.delay = 0.0},
                    .adverse_flow_delay = sim::ConstantDelaySpec{.delay = 0.0}},
        .master_seed = math::rng::Seed{1},
        .dt = 1.0,
        .tick_count = 10};

    REQUIRE(std::holds_alternative<sim::RandomWalkSpec>(config.process));

    config.process = sim::FactorModelSpec{.factor_count = 2};

    REQUIRE(std::holds_alternative<sim::FactorModelSpec>(config.process));
    CHECK(std::get<sim::FactorModelSpec>(config.process).factor_count == 2);
}

TEST_CASE("SimulationConfig::assets accepts an arbitrary number of assets, including zero",
          "[simulation][simulation_config]") {
    const sim::SimulationConfig empty{
        .assets = {},
        .correlation = math::Matrix(0, 0),
        .process = sim::RandomWalkSpec{},
        .strategy = minimal_strategy(0),
        .latency = {.quote_delay = sim::ConstantDelaySpec{.delay = 0.0},
                    .adverse_flow_delay = sim::ConstantDelaySpec{.delay = 0.0}},
        .master_seed = math::rng::Seed{0},
        .dt = 1.0,
        .tick_count = 0};

    CHECK(empty.assets.empty());
    // No validation at this level (consistent with every spec/config in this
    // zone): an inconsistent asset count is World's problem, not this
    // aggregate's, so this is only here to pin that nothing throws here.
}

TEST_CASE("SimulationConfig::dt and tick_count are the documented types",
          "[simulation][simulation_config]") {
    STATIC_REQUIRE(std::is_same_v<decltype(sim::SimulationConfig::dt), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::SimulationConfig::tick_count), std::int64_t>);
    STATIC_REQUIRE(std::is_same_v<decltype(sim::SimulationConfig::master_seed), math::rng::Seed>);
}

TEST_CASE("SimulationConfig::strategy is a complete, independent mm::StrategyConfig",
          "[simulation][simulation_config]") {
    sim::SimulationConfig config{
        .assets = {minimal_asset(100.0)},
        .correlation = math::Matrix::Identity(1, 1),
        .process = sim::RandomWalkSpec{},
        .strategy = minimal_strategy(1),
        .latency = {.quote_delay = sim::ConstantDelaySpec{.delay = 0.0},
                    .adverse_flow_delay = sim::ConstantDelaySpec{.delay = 0.0}},
        .master_seed = math::rng::Seed{1},
        .dt = 1.0,
        .tick_count = 10};

    config.strategy.gamma = 99.0;

    // Nothing else in config aliases strategy's data; mutating it in place
    // must not reach back into, say, a asset's own fields.
    CHECK(config.strategy.gamma == 99.0);
    CHECK(config.assets[0].initial_mid == 100.0);
}
