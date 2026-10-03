/// \file
/// Tests for simulation/world.hpp: construction from a SimulationConfig under
/// both PriceProcessSpec alternatives, forwarding of every per-asset field
/// (initial mid, drift, fees, volatility) to the right owner, the RNG
/// streams (same seed reproduces, different seed or different asset or
/// different subsystem diverges), the persistence of the handed-out engines,
/// and every consistency check the constructor is responsible for (it is
/// the first place with enough context to check config pieces against each
/// other).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/requote.hpp>
#include <mm/strategy_config.hpp>
#include <mm/types.hpp>

#include <math/rng/seed.hpp>
#include <math/types.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/simulation_config.hpp>
#include <simulation/world.hpp>

namespace sim = mms::simulation;
namespace math = mms::math;
namespace mm = mms::mm;

namespace {

sim::AssetSpec asset(mm::Price mid,
                     double volatility,
                     double mu = 0.0,
                     double fee_bps = 0.0,
                     std::optional<sim::FactorModelAssetData> factor = std::nullopt) {
    return sim::AssetSpec{.initial_mid = mid,
                          .maker_fee_bps = fee_bps,
                          .price_volatility = volatility,
                          .mu = mu,
                          .factor_model = std::move(factor),
                          .adverse_flow_lambda = 1.0,
                          .adverse_flow_size = sim::ExponentialSpec{.mean = 5.0}};
}

mm::StrategyConfig strategy_for(std::size_t assets) {
    const auto n = static_cast<Eigen::Index>(assets);
    return mm::StrategyConfig{
        .gamma = 2.0,
        .k = math::Vector::Constant(n, 1.0),
        .correlation = math::Matrix::Identity(n, n),
        .estimators = std::vector<mm::EstimatorSpec>(assets, mm::ConstantSpec{.sigma = 0.2}),
        .horizon = mm::ConstantHorizon{.tau = 1.0},
        .quote_sizes = std::vector<mm::Quantity>(assets, 10),
        .tick_sizes = std::vector<mm::Price>(assets, 0.01),
        .limits = {.max_abs_position = std::vector<mm::Quantity>(assets, 1000),
                   .max_portfolio_risk = std::nullopt},
        .min_samples = 0};
}

sim::SimulationConfig config_of(std::vector<sim::AssetSpec> assets,
                                sim::PriceProcessSpec process = sim::RandomWalkSpec{},
                                std::uint64_t seed = 42) {
    const std::size_t n = assets.size();
    const auto size = static_cast<Eigen::Index>(n);
    return sim::SimulationConfig{
        .assets = std::move(assets),
        .correlation = math::Matrix::Identity(size, size),
        .process = process,
        .strategy = strategy_for(n),
        .latency = {.quote_delay = sim::ConstantDelaySpec{.delay = 0.0},
                    .adverse_flow_delay = sim::ConstantDelaySpec{.delay = 0.0}},
        .master_seed = math::rng::Seed{seed},
        .dt = 1.0,
        .tick_count = 10};
}

sim::FactorModelAssetData factor_data(double beta, double idio) {
    return sim::FactorModelAssetData{.beta = math::Vector::Constant(1, beta),
                                     .idiosyncratic_volatility = idio};
}

} // namespace

TEST_CASE("A RandomWalk World is built with every part sized to the asset count",
          "[simulation][world]") {
    const sim::World world(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}));

    CHECK(world.asset_count() == 2);
    CHECK(world.price_process().asset_count() == 2);
    CHECK(world.pnl_ledger().asset_count() == 2);
    CHECK(world.strategy().asset_count() == 2);
}

TEST_CASE("A FactorModel World is built too", "[simulation][world]") {
    const sim::World world(config_of({asset(100.0, 0.2, 0.0, 0.0, factor_data(0.5, 0.1)),
                                      asset(50.0, 0.3, 0.0, 0.0, factor_data(0.8, 0.2))},
                                     sim::FactorModelSpec{.factor_count = 1}));

    CHECK(world.asset_count() == 2);
    CHECK(world.price_process().asset_count() == 2);
}

TEST_CASE("The price process starts at each asset's initial mid", "[simulation][world]") {
    const sim::World world(config_of({asset(100.0, 0.2), asset(50.0, 0.3), asset(25.0, 0.1)}));

    const math::Vector& prices = world.price_process().prices();

    REQUIRE(prices.size() == 3);
    CHECK(prices(0) == 100.0);
    CHECK(prices(1) == 50.0);
    CHECK(prices(2) == 25.0);
}

TEST_CASE("The ledger starts at each asset's initial mid, flat", "[simulation][world]") {
    const sim::World world(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}));

    CHECK(world.pnl_ledger().last_mid(0) == 100.0);
    CHECK(world.pnl_ledger().last_mid(1) == 50.0);
    CHECK(world.pnl_ledger().position(0).quantity() == 0);
    CHECK(world.pnl_ledger().position(1).quantity() == 0);
}

TEST_CASE("Each asset's maker fee reaches the ledger", "[simulation][world]") {
    sim::World world(config_of({asset(100.0, 0.2, 0.0, 2.0), asset(100.0, 0.2, 0.0, 0.0)}));

    world.pnl_ledger().on_fill(0, mm::Side::Buy, 100.0, 1000, 100.0); // notional 100000 at 2 bps
    world.pnl_ledger().on_fill(1, mm::Side::Buy, 100.0, 1000, 100.0);

    CHECK(world.pnl_ledger().breakdown(0).fees == Catch::Approx(-20.0).margin(1e-9));
    CHECK(world.pnl_ledger().breakdown(1).fees == 0.0);
}

TEST_CASE("The drift mu of each asset reaches the price process", "[simulation][world]") {
    // A huge drift against a negligible volatility: after dt = 1 each price
    // has moved by about its mu, so a wrong or swapped mu would show.
    sim::World world(config_of({asset(100.0, 1.0e-9, 10.0), asset(50.0, 1.0e-9, -4.0)}));

    world.price_process().advance(1.0);

    CHECK(world.price_process().prices()(0) == Catch::Approx(110.0).margin(1e-5));
    CHECK(world.price_process().prices()(1) == Catch::Approx(46.0).margin(1e-5));
}

TEST_CASE("Each asset gets its own empty MatchingEngine", "[simulation][world]") {
    sim::World world(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}));

    CHECK(world.matching_engine(0).book().empty(mm::Side::Buy));
    CHECK(world.matching_engine(1).book().empty(mm::Side::Sell));

    world.matching_engine(0).submit(mm::LimitOrder{.side = mm::Side::Buy,
                                                   .price = 99.0,
                                                   .quantity = 5,
                                                   .time_in_force = mm::TimeInForce::GTC});

    CHECK_FALSE(world.matching_engine(0).book().empty(mm::Side::Buy));
    CHECK(world.matching_engine(1).book().empty(mm::Side::Buy)); // the other asset is untouched
}

TEST_CASE("An unknown asset id throws std::out_of_range on every per-asset accessor",
          "[simulation][world]") {
    sim::World world(config_of({asset(100.0, 0.2)}));
    const sim::World& const_world = world;

    CHECK_THROWS_AS(world.matching_engine(1), std::out_of_range);
    CHECK_THROWS_AS(const_world.matching_engine(1), std::out_of_range);
    CHECK_THROWS_AS(world.adverse_flow_engine(1), std::out_of_range);
    CHECK_THROWS_AS(world.latency_engine(1), std::out_of_range);
}

TEST_CASE("The event queue is one empty queue for every kind of request, all assets",
          "[simulation][world]") {
    sim::World world(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}));

    REQUIRE(world.event_queue().empty());

    // Scheduled out of order, one of each kind, on different assets.
    world.event_queue().schedule(2.0, mm::CancelRequest{.asset = 1, .id = mm::OrderId{7}});
    world.event_queue().schedule(
        3.0,
        sim::AdverseOrder{.asset = 1,
                          .order = mm::MarketOrder{.side = mm::Side::Buy,
                                                   .quantity = 4,
                                                   .time_in_force = mm::MarketTimeInForce::IOC}});
    world.event_queue().schedule(
        1.0,
        mm::SubmitRequest{
            .asset = 0, .side = mm::Side::Buy, .quote = mm::Quote{.price = 99.0, .quantity = 5}});

    const auto first = world.event_queue().drain_until(1.5);
    REQUIRE(first.size() == 1);
    REQUIRE(std::holds_alternative<mm::SubmitRequest>(first[0]));
    CHECK(std::get<mm::SubmitRequest>(first[0]).asset == 0); // earliest first

    const auto rest = world.event_queue().drain_until(10.0);
    REQUIRE(rest.size() == 2);
    CHECK(std::holds_alternative<mm::CancelRequest>(rest[0]));   // due at 2.0
    REQUIRE(std::holds_alternative<sim::AdverseOrder>(rest[1])); // due at 3.0
    CHECK(std::get<sim::AdverseOrder>(rest[1]).order.quantity == 4);
    CHECK(world.event_queue().empty());
}

TEST_CASE("Events due at the same instant come out in the order they were scheduled",
          "[simulation][world]") {
    // This is what lets the tick loop make adverse flow hit the stale quotes
    // first: it schedules the adverse order before the quote requests.
    sim::World world(config_of({asset(100.0, 0.2)}));
    world.event_queue().schedule(
        1.0,
        sim::AdverseOrder{.asset = 0,
                          .order = mm::MarketOrder{.side = mm::Side::Sell,
                                                   .quantity = 1,
                                                   .time_in_force = mm::MarketTimeInForce::IOC}});
    world.event_queue().schedule(1.0, mm::CancelRequest{.asset = 0, .id = mm::OrderId{1}});

    const auto due = world.event_queue().drain_until(1.0);

    REQUIRE(due.size() == 2);
    CHECK(std::holds_alternative<sim::AdverseOrder>(due[0]));
    CHECK(std::holds_alternative<mm::CancelRequest>(due[1]));
}

TEST_CASE("Two Worlds built from the same config share price paths and RNG streams",
          "[simulation][world]") {
    sim::World first(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}, sim::RandomWalkSpec{}, 7));
    sim::World second(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}, sim::RandomWalkSpec{}, 7));

    first.price_process().advance(1.0);
    second.price_process().advance(1.0);

    CHECK(first.price_process().prices() == second.price_process().prices());
    CHECK(first.adverse_flow_engine(0)() == second.adverse_flow_engine(0)());
    CHECK(first.latency_engine(1)() == second.latency_engine(1)());
}

TEST_CASE("A different master seed gives different price paths and RNG streams",
          "[simulation][world]") {
    sim::World first(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}, sim::RandomWalkSpec{}, 7));
    sim::World second(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}, sim::RandomWalkSpec{}, 8));

    first.price_process().advance(1.0);
    second.price_process().advance(1.0);

    CHECK_FALSE(first.price_process().prices() == second.price_process().prices());
    CHECK(first.adverse_flow_engine(0)() != second.adverse_flow_engine(0)());
}

TEST_CASE("Different assets and different subsystems draw from different streams",
          "[simulation][world]") {
    sim::World world(config_of({asset(100.0, 0.2), asset(100.0, 0.2)}));

    const auto asset_0_adverse = world.adverse_flow_engine(0)();
    const auto asset_1_adverse = world.adverse_flow_engine(1)();
    const auto asset_0_latency = world.latency_engine(0)();

    CHECK(asset_0_adverse != asset_1_adverse);
    CHECK(asset_0_adverse != asset_0_latency);
}

TEST_CASE("The handed-out engines persist: the stream advances instead of restarting",
          "[simulation][world]") {
    sim::World world(config_of({asset(100.0, 0.2)}));

    math::rng::RngEngine& engine = world.adverse_flow_engine(0);
    const auto first = engine();
    const auto second =
        world.adverse_flow_engine(0)(); // asked again: same stream, one step further

    CHECK(&engine == &world.adverse_flow_engine(0));
    CHECK(first != second);
}

TEST_CASE("The constructor rejects a config with no assets", "[simulation][world]") {
    CHECK_THROWS_AS(sim::World(config_of({})), std::invalid_argument);
}

TEST_CASE("The constructor rejects a correlation that does not match the asset count",
          "[simulation][world]") {
    sim::SimulationConfig config = config_of({asset(100.0, 0.2), asset(50.0, 0.3)});
    config.correlation = math::Matrix::Identity(3, 3);

    CHECK_THROWS_AS(sim::World(config), std::invalid_argument);
}

TEST_CASE("The constructor rejects a correlation that is not positive definite",
          "[simulation][world]") {
    sim::SimulationConfig config = config_of({asset(100.0, 0.2), asset(50.0, 0.3)});
    math::Matrix correlation(2, 2);
    correlation << 1.0, 1.5, 1.5, 1.0;
    config.correlation = correlation;

    CHECK_THROWS_AS(sim::World(config), std::invalid_argument);
}

TEST_CASE("The constructor rejects a non-positive price volatility", "[simulation][world]") {
    CHECK_THROWS_AS(sim::World(config_of({asset(100.0, 0.0)})), std::invalid_argument);
    CHECK_THROWS_AS(sim::World(config_of({asset(100.0, -0.2)})), std::invalid_argument);
}

TEST_CASE("RandomWalkSpec rejects an asset that carries factor_model data", "[simulation][world]") {
    CHECK_THROWS_AS(sim::World(config_of({asset(100.0, 0.2, 0.0, 0.0, factor_data(0.5, 0.1))},
                                         sim::RandomWalkSpec{})),
                    std::invalid_argument);
}

TEST_CASE("FactorModelSpec rejects an asset missing its factor_model data", "[simulation][world]") {
    CHECK_THROWS_AS(
        sim::World(config_of({asset(100.0, 0.2)}, sim::FactorModelSpec{.factor_count = 1})),
        std::invalid_argument);
}

TEST_CASE("FactorModelSpec rejects a beta whose size is not the factor count",
          "[simulation][world]") {
    // factor_count says 2 factors, but the asset's beta has 1 entry.
    CHECK_THROWS_AS(sim::World(config_of({asset(100.0, 0.2, 0.0, 0.0, factor_data(0.5, 0.1))},
                                         sim::FactorModelSpec{.factor_count = 2})),
                    std::invalid_argument);
}

TEST_CASE("The constructor rejects a strategy sized for a different number of assets",
          "[simulation][world]") {
    sim::SimulationConfig config = config_of({asset(100.0, 0.2), asset(50.0, 0.3)});
    config.strategy = strategy_for(1); // internally consistent, but for 1 asset, not 2

    CHECK_THROWS_AS(sim::World(config), std::invalid_argument);
}

TEST_CASE("The constructor propagates the strategy's own validation", "[simulation][world]") {
    sim::SimulationConfig config = config_of({asset(100.0, 0.2)});
    config.strategy.gamma = 0.0; // AvellanedaStoikovStrategy rejects gamma <= 0

    CHECK_THROWS_AS(sim::World(config), std::invalid_argument);
}
