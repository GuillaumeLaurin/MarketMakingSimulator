/// \file
/// Tests for simulation/world.hpp: construction from a SimulationConfig under
/// both PriceProcessSpec alternatives, forwarding of every per-asset field
/// (initial mid, drift, fees, volatility) to the right owner, the RNG
/// streams (same seed reproduces, different seed or different asset or
/// different subsystem diverges), the persistence of the handed-out engines,
/// and every consistency check the constructor is responsible for (it is
/// the first place with enough context to check config pieces against each
/// other).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <mm/requote.hpp>
#include <mm/strategy_config.hpp>
#include <mm/types.hpp>

#include <math/process/dynamics.hpp>
#include <math/rng/seed.hpp>
#include <math/types.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/simulation_config.hpp>
#include <simulation/world.hpp>

#include "maker.hpp"

namespace sim = mms::simulation;
namespace math = mms::math;
namespace mm = mms::mm;

namespace {

sim::AssetSpec asset(mm::Price mid,
                     double volatility,
                     double mu = 0.0,
                     double fee_bps = 0.0,
                     std::optional<sim::FactorModelAssetData> factor = std::nullopt) {
    return sim::AssetSpec{.symbol = "",
                          .initial_mid = mid,
                          .maker_fee_bps = fee_bps,
                          .price_volatility = volatility,
                          .mu = mu,
                          .factor_model = std::move(factor)};
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
                                sim::PriceProcessSpec process =
                                    sim::RandomWalkSpec{
                                        .dynamics = mms::math::process::Dynamics::Arithmetic},
                                std::uint64_t seed = 42) {
    // A test that does not care about names leaves the symbol empty and gets "ASSET<i>".
    for (std::size_t i = 0; i < assets.size(); ++i) {
        if (assets[i].symbol.empty()) {
            assets[i].symbol = "ASSET" + std::to_string(i);
        }
    }
    const std::size_t n = assets.size();
    const auto size = static_cast<Eigen::Index>(n);
    return sim::SimulationConfig{.assets = std::move(assets),
                                 .correlation = math::Matrix::Identity(size, size),
                                 .process = process,
                                 .agents = {sim::tests::maker_agent(strategy_for(n))},
                                 .master_seed = math::rng::Seed{seed},
                                 .dt = 1.0,
                                 .tick_count = 10};
}

sim::FactorModelAssetData factor_data(double beta, double idio) {
    return sim::FactorModelAssetData{.beta = math::Vector::Constant(1, beta),
                                     .idiosyncratic_volatility = idio};
}

sim::AssetSpec named(std::string symbol, sim::AssetSpec spec) {
    spec.symbol = std::move(symbol);
    return spec;
}

// The price of one asset after `ticks` ticks of dt = 1, read by symbol.
double price_after(const sim::SimulationConfig& config, const std::string& symbol, int ticks) {
    sim::World world(config);
    for (int tick = 0; tick < ticks; ++tick) {
        world.price_process().advance(1.0);
    }
    for (std::size_t i = 0; i < config.assets.size(); ++i) {
        if (config.assets[i].symbol == symbol) {
            return world.price_process().prices()(static_cast<Eigen::Index>(i));
        }
    }
    FAIL("no such symbol in the config");
    return 0.0;
}

} // namespace

TEST_CASE("A RandomWalk World is built with every part sized to the asset count",
          "[simulation][world]") {
    const sim::World world(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}));

    CHECK(world.asset_count() == 2);
    CHECK(world.price_process().asset_count() == 2);
    REQUIRE(world.agent_count() == 1);
    CHECK(world.agent(0).account().ledger().asset_count() == 2);
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

TEST_CASE("An agent's ledger starts at each asset's initial mid, flat", "[simulation][world]") {
    const sim::World world(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}));
    const mm::PnlLedger& ledger = world.agent(0).account().ledger();

    CHECK(ledger.last_mid(0) == 100.0);
    CHECK(ledger.last_mid(1) == 50.0);
    CHECK(ledger.position(0).quantity() == 0);
    CHECK(ledger.position(1).quantity() == 0);
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
}

TEST_CASE("The event queue is one empty queue for every kind of message, all assets",
          "[simulation][world]") {
    sim::World world(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}));

    REQUIRE(world.event_queue().empty());

    // Scheduled out of order, one of each kind, on different assets.
    world.event_queue().schedule(
        2.0,
        sim::CancelArrival{.agent = 0, .asset = 1, .id = mm::OrderId{7}, .side = mm::Side::Buy});
    world.event_queue().schedule(
        3.0,
        sim::SubmitArrival{
            .agent = 0,
            .ticket = sim::Ticket{2},
            .order = mms::agents::SubmitMarket{.asset = 1, .side = mm::Side::Buy, .quantity = 4}});
    world.event_queue().schedule(
        1.0,
        sim::SubmitArrival{.agent = 0,
                           .ticket = sim::Ticket{1},
                           .order = mms::agents::SubmitLimit{
                               .asset = 0, .side = mm::Side::Buy, .price = 99.0, .quantity = 5}});

    const auto first = world.event_queue().drain_until(1.5);
    REQUIRE(first.size() == 1);
    REQUIRE(std::holds_alternative<sim::SubmitArrival>(first[0]));
    CHECK(std::get<mms::agents::SubmitLimit>(std::get<sim::SubmitArrival>(first[0]).order).asset ==
          0); // earliest first

    const auto rest = world.event_queue().drain_until(10.0);
    REQUIRE(rest.size() == 2);
    CHECK(std::holds_alternative<sim::CancelArrival>(rest[0]));   // due at 2.0
    REQUIRE(std::holds_alternative<sim::SubmitArrival>(rest[1])); // due at 3.0
    CHECK(
        std::get<mms::agents::SubmitMarket>(std::get<sim::SubmitArrival>(rest[1]).order).quantity ==
        4);
    CHECK(world.event_queue().empty());
}

TEST_CASE("Events due at the same instant come out in the order they were scheduled",
          "[simulation][world]") {
    // This is what lets an agent listed first reach the exchange first.
    sim::World world(config_of({asset(100.0, 0.2)}));
    world.event_queue().schedule(
        1.0,
        sim::SubmitArrival{
            .agent = 0,
            .ticket = sim::Ticket{1},
            .order = mms::agents::SubmitMarket{.asset = 0, .side = mm::Side::Sell, .quantity = 1}});
    world.event_queue().schedule(
        1.0,
        sim::CancelArrival{.agent = 0, .asset = 0, .id = mm::OrderId{1}, .side = mm::Side::Buy});

    const auto due = world.event_queue().drain_until(1.0);

    REQUIRE(due.size() == 2);
    CHECK(std::holds_alternative<sim::SubmitArrival>(due[0]));
    CHECK(std::holds_alternative<sim::CancelArrival>(due[1]));
}

TEST_CASE("Two Worlds built from the same config share price paths and RNG streams",
          "[simulation][world]") {
    sim::World first(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}, sim::RandomWalkSpec{}, 7));
    sim::World second(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}, sim::RandomWalkSpec{}, 7));

    first.price_process().advance(1.0);
    second.price_process().advance(1.0);

    CHECK(first.price_process().prices() == second.price_process().prices());
}

TEST_CASE("A different master seed gives different price paths and RNG streams",
          "[simulation][world]") {
    sim::World first(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}, sim::RandomWalkSpec{}, 7));
    sim::World second(config_of({asset(100.0, 0.2), asset(50.0, 0.3)}, sim::RandomWalkSpec{}, 8));

    first.price_process().advance(1.0);
    second.price_process().advance(1.0);

    CHECK_FALSE(first.price_process().prices() == second.price_process().prices());
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

TEST_CASE(
    "The constructor rejects a market maker whose strategy is sized for another number of assets",
    "[simulation][world]") {
    sim::SimulationConfig config = config_of({asset(100.0, 0.2), asset(50.0, 0.3)});
    config.agents[0] =
        sim::tests::maker_agent(strategy_for(1)); // internally consistent, but for 1 asset, not 2

    CHECK_THROWS_AS(sim::World(config), std::invalid_argument);
}

TEST_CASE("The constructor propagates an agent's own validation", "[simulation][world]") {
    sim::SimulationConfig config = config_of({asset(100.0, 0.2)});
    std::get<mms::agents::MarketMakerSpec>(config.agents[0].behavior).strategy.gamma =
        0.0; // AvellanedaStoikovStrategy rejects gamma <= 0

    CHECK_THROWS_AS(sim::World(config), std::invalid_argument);
}

// ------------------------------------------------------------------ symbols

TEST_CASE("An asset without a symbol is rejected", "[simulation][world][symbol]") {
    sim::SimulationConfig config = config_of({asset(100.0, 0.2), asset(50.0, 0.3)});
    config.assets[1].symbol.clear();

    CHECK_THROWS_AS(sim::World(config), std::invalid_argument);
}

TEST_CASE("Two assets with the same symbol are rejected, and the message names it",
          "[simulation][world][symbol]") {
    const sim::SimulationConfig config = config_of({named("AAPL", asset(100.0, 0.2)),
                                                    named("MSFT", asset(50.0, 0.3)),
                                                    named("AAPL", asset(70.0, 0.1))});

    CHECK_THROWS_AS(sim::World(config), std::invalid_argument);
    CHECK_THROWS_WITH(sim::World(config), Catch::Matchers::ContainsSubstring("AAPL"));
}

TEST_CASE("Symbols differing only by case are two assets", "[simulation][world][symbol]") {
    const sim::SimulationConfig config =
        config_of({named("AAPL", asset(100.0, 0.2)), named("aapl", asset(50.0, 0.3))});

    CHECK_NOTHROW(sim::World(config));
}

TEST_CASE("Renaming an asset changes its price path", "[simulation][world][symbol]") {
    const sim::SimulationConfig a = config_of({named("AAPL", asset(100.0, 0.2))});
    const sim::SimulationConfig b = config_of({named("AAPL2", asset(100.0, 0.2))});

    CHECK(price_after(a, "AAPL", 3) != price_after(b, "AAPL2", 3));
}

TEST_CASE("A random-walk price path does not depend on the other assets in the basket",
          "[simulation][world][symbol]") {
    // Identity correlation: nothing couples the assets, so only the symbol
    // (the stream) and the asset's own parameters can matter.
    const sim::SimulationConfig small =
        config_of({named("AAPL", asset(100.0, 0.2, 0.01)), named("MSFT", asset(50.0, 0.3))});
    const sim::SimulationConfig bigger = config_of({named("NEWCO", asset(20.0, 0.5)),
                                                    named("MSFT", asset(50.0, 0.3)),
                                                    named("AAPL", asset(100.0, 0.2, 0.01))});

    CHECK(price_after(small, "AAPL", 25) == price_after(bigger, "AAPL", 25));
    CHECK(price_after(small, "MSFT", 25) == price_after(bigger, "MSFT", 25));
    // A path that moves at all, or this would hold for the wrong reason.
    CHECK(price_after(small, "AAPL", 25) != 100.0);
}

TEST_CASE("A factor-model price path does not depend on the other assets either",
          "[simulation][world][symbol]") {
    const sim::FactorModelSpec one_factor{.factor_count = 1,
                                          .dynamics = mms::math::process::Dynamics::Arithmetic};
    const sim::SimulationConfig small =
        config_of({named("AAPL", asset(100.0, 0.2, 0.0, 0.0, factor_data(0.5, 0.1))),
                   named("MSFT", asset(50.0, 0.3, 0.0, 0.0, factor_data(0.8, 0.2)))},
                  one_factor);
    const sim::SimulationConfig bigger =
        config_of({named("MSFT", asset(50.0, 0.3, 0.0, 0.0, factor_data(0.8, 0.2))),
                   named("NEWCO", asset(20.0, 0.5, 0.0, 0.0, factor_data(1.5, 0.4))),
                   named("AAPL", asset(100.0, 0.2, 0.0, 0.0, factor_data(0.5, 0.1)))},
                  one_factor);

    CHECK(price_after(small, "AAPL", 25) == price_after(bigger, "AAPL", 25));
    CHECK(price_after(small, "MSFT", 25) == price_after(bigger, "MSFT", 25));
    CHECK(price_after(small, "AAPL", 25) != 100.0);
}

TEST_CASE("Each common factor is its own stream", "[simulation][world][symbol]") {
    // Asset A loads only on factor 0, asset B only on factor 1, neither has
    // noise of its own: their moves are the two factors' draws. Two factors
    // sharing one stream would move them identically.
    math::Vector on_first(2);
    on_first << 1.0, 0.0;
    math::Vector on_second(2);
    on_second << 0.0, 1.0;
    const sim::SimulationConfig config = config_of(
        {named("A",
               asset(100.0,
                     0.2,
                     0.0,
                     0.0,
                     sim::FactorModelAssetData{.beta = on_first, .idiosyncratic_volatility = 0.0})),
         named(
             "B",
             asset(100.0,
                   0.2,
                   0.0,
                   0.0,
                   sim::FactorModelAssetData{.beta = on_second, .idiosyncratic_volatility = 0.0}))},
        sim::FactorModelSpec{.factor_count = 2,
                             .dynamics = mms::math::process::Dynamics::Arithmetic});

    const double move_a = price_after(config, "A", 10) - 100.0;
    const double move_b = price_after(config, "B", 10) - 100.0;

    CHECK(move_a != 0.0);
    CHECK(move_b != 0.0);
    CHECK(move_a != move_b);
}

TEST_CASE("Specs are geometric by default: mu is a return rate", "[simulation][world][dynamics]") {
    // Volatility next to zero, so the price is the drift alone: 100 * exp(0.1)
    // when geometric, 100 + 0.1 when arithmetic.
    const double geometric =
        price_after(config_of({asset(100.0, 1e-9, 0.1)}, sim::RandomWalkSpec{}), "ASSET0", 1);
    CHECK(geometric == Catch::Approx(100.0 * std::exp(0.1)));

    const sim::SimulationConfig factor_config =
        config_of({asset(100.0, 1e-9, 0.1, 0.0, factor_data(0.0, 1e-9))},
                  sim::FactorModelSpec{.factor_count = 1});
    CHECK(price_after(factor_config, "ASSET0", 1) == Catch::Approx(100.0 * std::exp(0.1)));
}

TEST_CASE("Arithmetic dynamics keep mu in price units", "[simulation][world][dynamics]") {
    using mms::math::process::Dynamics;
    const sim::RandomWalkSpec arithmetic{.dynamics = Dynamics::Arithmetic};

    CHECK(price_after(config_of({asset(100.0, 1e-9, 0.1)}, arithmetic), "ASSET0", 1) ==
          Catch::Approx(100.1));

    const sim::SimulationConfig factor_config =
        config_of({asset(100.0, 1e-9, 0.1, 0.0, factor_data(0.0, 1e-9))},
                  sim::FactorModelSpec{.factor_count = 1, .dynamics = Dynamics::Arithmetic});
    CHECK(price_after(factor_config, "ASSET0", 1) == Catch::Approx(100.1));
}

TEST_CASE("The dynamics reaches the factor model too, and the variance counts beta and idio",
          "[simulation][world][dynamics]") {
    // With mu = 0 the geometric price keeps E[p] = p0, which only holds if the
    // Ito term is (beta^2 + idio^2) / 2 = 0.05. Left out, the mean would be 105.
    // The standard error over 1000 seeds is about 1.
    math::Vector beta(1);
    beta << 0.3;
    const sim::SimulationConfig config =
        config_of({asset(100.0,
                         0.2,
                         0.0,
                         0.0,
                         sim::FactorModelAssetData{.beta = beta, .idiosyncratic_volatility = 0.1})},
                  sim::FactorModelSpec{.factor_count = 1});
    double sum = 0.0;
    constexpr int runs = 1000;
    for (int seed = 0; seed < runs; ++seed) {
        sim::SimulationConfig seeded = config;
        seeded.master_seed = math::rng::Seed{static_cast<std::uint64_t>(seed)};
        sum += price_after(seeded, "ASSET0", 1);
    }
    CHECK(sum / runs == Catch::Approx(100.0).margin(3.0));
}

TEST_CASE("Geometric prices never reach zero, however volatile", "[simulation][world][dynamics]") {
    sim::World world(config_of({asset(1.0, 2.0), asset(5.0, 1.5)}, sim::RandomWalkSpec{}));

    for (int tick = 0; tick < 300; ++tick) {
        world.price_process().advance(1.0);
        CHECK(world.price_process().prices().minCoeff() >= 0.0);
    }
}

TEST_CASE("A geometric World rejects an initial mid that is not positive",
          "[simulation][world][dynamics]") {
    CHECK_THROWS_AS(sim::World(config_of({asset(0.0, 0.2)}, sim::RandomWalkSpec{})),
                    std::invalid_argument);
    CHECK_THROWS_AS(sim::World(config_of({asset(-5.0, 0.2)}, sim::RandomWalkSpec{})),
                    std::invalid_argument);

    const sim::SimulationConfig factor_config =
        config_of({asset(0.0, 0.2, 0.0, 0.0, factor_data(0.5, 0.1))},
                  sim::FactorModelSpec{.factor_count = 1});
    CHECK_THROWS_AS(sim::World(factor_config), std::invalid_argument);
}

// ------------------------------------------------------------------- agents

namespace {

sim::SimulationConfig config_with_agents(std::vector<std::string> names) {
    sim::SimulationConfig config = config_of({asset(100.0, 0.2), asset(50.0, 0.3)});
    config.agents.clear();
    for (std::string& name : names) {
        config.agents.push_back(
            sim::tests::maker_agent(strategy_for(2),
                                    1,
                                    sim::UniformDelaySpec{.min = 0.0, .max = 9.0},
                                    1.0,
                                    std::move(name)));
    }
    return config;
}

} // namespace

TEST_CASE("The world builds one runtime per agent, in the order of the config",
          "[simulation][world][agents]") {
    const sim::World world(config_with_agents({"first", "second", "third"}));

    REQUIRE(world.agent_count() == 3);
    for (sim::AgentId id = 0; id < 3; ++id) {
        CHECK(world.agent(id).id() == id);
    }
    CHECK(world.agent(0).spec().profile.name == "first");
    CHECK(world.agent(2).spec().profile.name == "third");
    CHECK_THROWS_AS(world.agent(3), std::out_of_range);
}

TEST_CASE("A world may have no agent at all", "[simulation][world][agents]") {
    const sim::World world(config_with_agents({}));

    CHECK(world.agent_count() == 0);
}

TEST_CASE("Every agent has its own accounts, over the assets of the world",
          "[simulation][world][agents]") {
    const sim::World world(config_with_agents({"a", "b"}));

    CHECK(world.agent(0).account().ledger().asset_count() == 2);
    CHECK(world.agent(1).account().ledger().last_mid(1) == 50.0);
    CHECK(&world.agent(0).account() != &world.agent(1).account());
}

TEST_CASE("The world refuses an agent setup that validate_agent_setup refuses, before anything "
          "else",
          "[simulation][world][agents]") {
    sim::SimulationConfig twins = config_with_agents({"same", "same"});
    CHECK_THROWS_WITH(sim::World(twins), Catch::Matchers::ContainsSubstring("share the name"));

    sim::SimulationConfig slow = config_with_agents({"a"});
    slow.agents[0].timing.decision_period = 0.0;
    CHECK_THROWS_AS(sim::World(slow), std::invalid_argument);

    sim::SimulationConfig no_tick = config_with_agents({"a"});
    no_tick.assets[0].tick_size = 0.0;
    CHECK_THROWS_AS(sim::World(no_tick), std::invalid_argument);
}

TEST_CASE("The world refuses a market maker with a negative requote threshold",
          "[simulation][world][agents]") {
    sim::SimulationConfig config = config_with_agents({"a"});
    std::get<mms::agents::MarketMakerSpec>(config.agents[0].behavior).requote_threshold_ticks = -1;

    CHECK_THROWS_AS(sim::World(config), std::invalid_argument);
}

TEST_CASE("An agent's streams depend on its name, not on its place in the list",
          "[simulation][world][agents]") {
    // The runtimes expose no engine, so compare what the delays say: two worlds in which
    // "b" sits at different positions and has different company draw the same delays.
    const auto first_quotes = [](std::vector<std::string> names, std::size_t position) {
        sim::World world(config_with_agents(std::move(names)));
        const std::vector<mms::agents::AssetView> markets = [&] {
            std::vector<mms::agents::AssetView> views(2);
            views[0].signal = 100.0;
            views[1].signal = 50.0;
            return views;
        }();
        const sim::ActResult result = world.agent(position).act(1, 1.0, markets);
        std::vector<double> prices;
        for (const sim::Scheduled& scheduled : result.scheduled) {
            const auto& arrival = std::get<sim::SubmitArrival>(scheduled.event);
            prices.push_back(std::get<mms::agents::SubmitLimit>(arrival.order).price);
            prices.push_back(scheduled.due);
        }
        return prices;
    };

    CHECK(first_quotes({"b"}, 0) == first_quotes({"a", "b", "c"}, 1));
}

TEST_CASE("Two agents with different names draw different delays", "[simulation][world][agents]") {
    sim::World world(config_with_agents({"a", "b"}));
    std::vector<mms::agents::AssetView> markets(2);
    markets[0].signal = 100.0;
    markets[1].signal = 50.0;

    const sim::ActResult first = world.agent(0).act(1, 1.0, markets);
    const sim::ActResult second = world.agent(1).act(1, 1.0, markets);

    REQUIRE(first.scheduled.size() == second.scheduled.size());
    REQUIRE_FALSE(first.scheduled.empty());
    CHECK(first.scheduled[0].due != second.scheduled[0].due);
}

TEST_CASE(
    "The world refuses an informed trader whose parameters are invalid, before the first tick",
    "[simulation][world][agents]") {
    sim::SimulationConfig config = config_with_agents({"a"});
    config.agents[0] = sim::tests::informed_agent(1.0, 2.0); // a correlation is in [0, 1]

    CHECK_THROWS_AS(sim::World(config), std::invalid_argument);

    config.agents[0] = sim::tests::informed_agent(1.0, 0.5, sim::ExponentialDelaySpec{.mean = 0.0});
    CHECK_THROWS_AS(sim::World(config), std::invalid_argument); // and a delay is valid
}

TEST_CASE("A factor trader needs a factor model with one factor per sensitivity",
          "[simulation][world][agents]") {
    const auto trader = [](std::size_t sensitivities) {
        return sim::AgentSpec{
            .profile = {.name = "factors", .initial_cash = 1e6, .can_short = true},
            .timing = {},
            .behavior = mms::agents::FactorTraderSpec{.sensitivities =
                                                          std::vector<double>(sensitivities, 1.0),
                                                      .threshold = 0.0,
                                                      .order_size = 1,
                                                      .max_position = 5}};
    };

    // A random walk has no factors.
    sim::SimulationConfig walk = config_of({asset(100.0, 0.2)});
    walk.agents = {trader(1)};
    CHECK_THROWS_AS(sim::World(walk), std::invalid_argument);

    // A factor model with the wrong number of them.
    sim::SimulationConfig two = config_of({asset(100.0, 0.2, 0.0, 0.0, factor_data(0.5, 0.1))},
                                          sim::FactorModelSpec{.factor_count = 1});
    two.agents = {trader(2)};
    CHECK_THROWS_AS(sim::World(two), std::invalid_argument);

    // And the right number.
    sim::SimulationConfig right = config_of({asset(100.0, 0.2, 0.0, 0.0, factor_data(0.5, 0.1))},
                                            sim::FactorModelSpec{.factor_count = 1});
    right.agents = {trader(1)};
    CHECK_NOTHROW(sim::World(right));
}
