/// \file
/// Tests for simulation/simulation_config.hpp: a plain aggregate assembling
/// every piece this zone has built so far, so this mostly pins field
/// exactness, types and defaults.

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <agents/noise_trader.hpp>
#include <catch2/catch_test_macros.hpp>

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
    return sim::AssetSpec{.symbol = "ASSET" + std::to_string(static_cast<int>(mid)),
                          .initial_mid = mid,
                          .price_volatility = 0.2,
                          .factor_model = std::nullopt,
                          .adverse_flow_size = sim::ExponentialSpec{.mean = 5.0}};
}

sim::AgentSpec minimal_agent(std::string name) {
    return sim::AgentSpec{
        .profile = {.name = std::move(name), .initial_cash = 100.0},
        .timing = {},
        .behavior = mms::agents::NoiseTraderSpec{.lambda = 1.0,
                                                 .size = sim::ExponentialSpec{.mean = 5.0},
                                                 .market_probability = 0.5,
                                                 .max_offset_ticks = 3}};
}

} // namespace

TEST_CASE("SimulationConfig holds every top-level field exactly",
          "[simulation][simulation_config]") {
    const sim::SimulationConfig config{
        .assets = {minimal_asset(100.0), minimal_asset(50.0)},
        .correlation = math::Matrix::Identity(2, 2),
        .process = sim::RandomWalkSpec{},
        .latency = {.adverse_flow_delay = sim::ConstantDelaySpec{.delay = 0.0}},
        .agents = {minimal_agent("a"), minimal_agent("b"), minimal_agent("c")},
        .account_rules = {.short_collateral_rate = 0.5, .market_order_buffer = 0.1},
        .book_depth = 8,
        .master_seed = math::rng::Seed{42},
        .dt = 0.5,
        .tick_count = 1000};

    REQUIRE(config.assets.size() == 2);
    CHECK(config.assets[0].initial_mid == 100.0);
    CHECK(config.assets[1].initial_mid == 50.0);
    CHECK(config.correlation.rows() == 2);
    REQUIRE(config.agents.size() == 3);
    CHECK(config.agents[2].profile.name == "c");
    CHECK(config.account_rules.short_collateral_rate == 0.5);
    CHECK(config.account_rules.market_order_buffer == 0.1);
    CHECK(config.book_depth == 8);
    CHECK(config.master_seed == math::rng::Seed{42});
    CHECK(config.dt == 0.5);
    CHECK(config.tick_count == 1000);
}

TEST_CASE("SimulationConfig::process is a whole-simulation choice, holding whichever "
          "PriceProcessSpec alternative it was built from",
          "[simulation][simulation_config]") {
    sim::SimulationConfig config{.assets = {minimal_asset(100.0)},
                                 .correlation = math::Matrix::Identity(1, 1),
                                 .process = sim::RandomWalkSpec{},
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
    const sim::SimulationConfig empty{.assets = {},
                                      .correlation = math::Matrix(0, 0),
                                      .process = sim::RandomWalkSpec{},
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

TEST_CASE("SimulationConfig::agents keeps the order it is given, and an agent's behavior "
          "is its own",
          "[simulation][simulation_config]") {
    sim::SimulationConfig config{.assets = {minimal_asset(100.0)},
                                 .correlation = math::Matrix::Identity(1, 1),
                                 .process = sim::RandomWalkSpec{},
                                 .agents = {minimal_agent("first"), minimal_agent("second")},
                                 .master_seed = math::rng::Seed{1},
                                 .dt = 1.0,
                                 .tick_count = 10};

    std::get<mms::agents::NoiseTraderSpec>(config.agents[1].behavior).lambda = 99.0;

    CHECK(config.agents[0].profile.name == "first");
    CHECK(std::get<mms::agents::NoiseTraderSpec>(config.agents[0].behavior).lambda == 1.0);
    CHECK(std::get<mms::agents::NoiseTraderSpec>(config.agents[1].behavior).lambda == 99.0);
}
