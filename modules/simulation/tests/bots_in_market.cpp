/// \file
/// Every kind of agent in one market, driven by the real loop. The agents are the real ones, so
/// nothing about what they decide is scripted; what is checked are the things that must hold
/// whatever they decide: what the books hold is exactly what the accounts believe they hold,
/// nothing is created or lost between accounts, and the run replays exactly.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/strategy_config.hpp>
#include <mm/types.hpp>

#include <math/process/dynamics.hpp>
#include <math/rng/seed.hpp>
#include <math/types.hpp>
#include <simulation/simulation_config.hpp>
#include <simulation/size_spec.hpp>
#include <simulation/tick_loop.hpp>
#include <simulation/world.hpp>

#include "maker.hpp"

namespace sim = mms::simulation;
namespace agents = mms::agents;
namespace math = mms::math;
namespace mm = mms::mm;

namespace {

constexpr double cash = 1e9;

sim::AgentSpec trader(std::string name, agents::AgentBehavior behavior, double period = 1.0) {
    return sim::AgentSpec{
        .profile = {.name = std::move(name), .initial_cash = cash, .can_short = true},
        .timing = {.decision_period = period},
        .behavior = std::move(behavior)};
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

/// Two assets driven by two common factors, a market maker and one agent of every other kind.
sim::SimulationConfig crowded_market(std::uint64_t seed) {
    std::vector<sim::AssetSpec> assets;
    for (const double mid : {100.0, 40.0}) {
        assets.push_back(sim::AssetSpec{.symbol = "ASSET" + std::to_string(static_cast<int>(mid)),
                                        .initial_mid = mid,
                                        .price_volatility = 0.8,
                                        .factor_model = sim::FactorModelAssetData{
                                            .beta = (math::Vector(2) << 0.6, -0.3).finished(),
                                            .idiosyncratic_volatility = 0.3}});
    }
    sim::SimulationConfig config{
        .assets = std::move(assets),
        .correlation = math::Matrix::Identity(2, 2),
        .process = sim::FactorModelSpec{.factor_count = 2,
                                        .dynamics = math::process::Dynamics::Arithmetic},
        .master_seed = math::rng::Seed{seed},
        .dt = 1.0,
        .tick_count = 200};
    config.agents = {
        trader("informed",
               agents::InformedFlowSpec{.lambda = 1.5,
                                        .direction_correlation = 0.6,
                                        .size = sim::UniformIntSpec{.min = 1, .max = 4}}),
        sim::tests::maker_agent(strategy_for(2), 1, {}, 1.0, "maker"),
        trader("noise",
               agents::NoiseTraderSpec{.lambda = 2.0,
                                       .size = sim::UniformIntSpec{.min = 1, .max = 3},
                                       .market_probability = 0.3,
                                       .max_offset_ticks = 4}),
        trader("trend",
               agents::TrendFollowerSpec{
                   .half_life = 6.0, .threshold = 0.001, .order_size = 2, .max_position = 20},
               2.0),
        trader("reverter",
               agents::MeanReverterSpec{
                   .half_life = 6.0, .threshold = 0.001, .order_size = 2, .max_position = 20},
               3.0),
        trader("factors",
               agents::FactorTraderSpec{.sensitivities = {1.0, -0.5},
                                        .threshold = 0.1,
                                        .order_size = 1,
                                        .max_position = 10})};
    return config;
}

/// What the books hold, set against what the accounts believe their orders are.
void check_books_against_accounts(const sim::World& world, const sim::TickLoop& loop) {
    std::size_t resting_in_books = 0;
    for (mm::AssetId asset = 0; asset < world.asset_count(); ++asset) {
        const mm::OrderBook& book = world.matching_engine(asset).book();
        resting_in_books += book.size(mm::Side::Buy) + book.size(mm::Side::Sell);

        for (const mm::Side side : {mm::Side::Buy, mm::Side::Sell}) {
            // What the agents say rests at each price, all of them together.
            std::map<mm::Price, mm::Quantity> claimed;
            for (sim::AgentId id = 0; id < world.agent_count(); ++id) {
                for (const agents::OpenOrder& order : world.agent(id).orders().orders()) {
                    if (order.asset == asset && order.side == side &&
                        order.state != agents::OrderState::Submitting) {
                        claimed[order.price] += order.remaining;
                    }
                }
            }
            for (const auto& [price, quantity] : claimed) {
                CHECK(book.depth_at(side, price) == quantity);
            }
            // And nothing rests that nobody claims.
            for (const auto& level : book.levels(side, 1000)) {
                CHECK(claimed.contains(level.price));
            }
        }
    }
    CHECK(loop.recorded_orders() == resting_in_books);
}

/// Nothing is created or lost between accounts: positions add up to zero in every asset, and
/// so does the cash change (there are no fees here).
void check_conservation(const sim::World& world, double initial_cash_total) {
    double cash_total = 0.0;
    for (mm::AssetId asset = 0; asset < world.asset_count(); ++asset) {
        mm::Quantity net = 0;
        for (sim::AgentId id = 0; id < world.agent_count(); ++id) {
            net += world.agent(id).account().ledger().position(asset).quantity();
        }
        CHECK(net == 0);
    }
    for (sim::AgentId id = 0; id < world.agent_count(); ++id) {
        cash_total += world.agent(id).account().cash();
    }
    CHECK(cash_total == Catch::Approx(initial_cash_total).epsilon(1e-9));
}

struct Outcome {
    std::vector<mm::Quantity> positions;
    std::vector<double> cash;
    std::size_t resting = 0;

    friend bool operator==(const Outcome&, const Outcome&) = default;
};

Outcome run(std::uint64_t seed) {
    const sim::SimulationConfig config = crowded_market(seed);
    sim::World world(config);
    sim::TickLoop loop(world, config);
    for (std::int64_t tick = 0; tick < config.tick_count; ++tick) {
        loop.step();
    }
    Outcome outcome;
    for (sim::AgentId id = 0; id < world.agent_count(); ++id) {
        for (mm::AssetId asset = 0; asset < world.asset_count(); ++asset) {
            outcome.positions.push_back(
                world.agent(id).account().ledger().position(asset).quantity());
        }
        outcome.cash.push_back(world.agent(id).account().cash());
    }
    outcome.resting = loop.recorded_orders();
    return outcome;
}

} // namespace

TEST_CASE("Every kind of agent trades in one market, and the books and accounts always agree",
          "[simulation][bots]") {
    for (const std::uint64_t seed : {1U, 2U, 3U}) {
        const sim::SimulationConfig config = crowded_market(seed);
        sim::World world(config);
        sim::TickLoop loop(world, config);
        const double initial_total = cash * static_cast<double>(config.agents.size() - 1) + 1e12;

        for (std::int64_t tick = 0; tick < config.tick_count; ++tick) {
            loop.step();
            check_books_against_accounts(world, loop);
            check_conservation(world, initial_total);
        }

        // Each one really took part.
        for (sim::AgentId id = 0; id < world.agent_count(); ++id) {
            const sim::Account& account = world.agent(id).account();
            const bool traded = account.cash() != account.profile().initial_cash;
            INFO("agent " << account.profile().name << ", seed " << seed);
            CHECK(traded);
        }
    }
}

TEST_CASE("A crowded market replays exactly, and differs with the seed", "[simulation][bots]") {
    CHECK(run(7) == run(7));
    CHECK_FALSE(run(7) == run(8));
}

TEST_CASE("Bots respect their position limits, whatever the market does", "[simulation][bots]") {
    const sim::SimulationConfig config = crowded_market(5);
    sim::World world(config);
    sim::TickLoop loop(world, config);

    for (std::int64_t tick = 0; tick < config.tick_count; ++tick) {
        loop.step();
        for (const sim::AgentId id : {3U, 4U}) { // the trend follower and the reverter
            for (mm::AssetId asset = 0; asset < 2; ++asset) {
                CHECK(std::abs(world.agent(id).account().ledger().position(asset).quantity()) <=
                      20);
            }
        }
        for (mm::AssetId asset = 0; asset < 2; ++asset) {
            CHECK(std::abs(world.agent(5).account().ledger().position(asset).quantity()) <= 10);
        }
    }
}

TEST_CASE("Information delays and order delays change nothing about the invariants",
          "[simulation][bots]") {
    sim::SimulationConfig config = crowded_market(11);
    for (sim::AgentSpec& agent : config.agents) {
        agent.timing.order_delay = sim::UniformDelaySpec{.min = 0.0, .max = 2.5};
        agent.timing.info_delay = sim::ExponentialDelaySpec{.mean = 1.0};
    }
    sim::World world(config);
    sim::TickLoop loop(world, config);
    const double initial_total = cash * static_cast<double>(config.agents.size() - 1) + 1e12;

    for (std::int64_t tick = 0; tick < config.tick_count; ++tick) {
        loop.step();
        check_books_against_accounts(world, loop);
        check_conservation(world, initial_total);
    }
}
