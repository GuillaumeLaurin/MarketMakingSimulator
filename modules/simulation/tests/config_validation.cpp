/// \file
/// Tests for simulation/config_validation.hpp: what validate_agent_setup accepts and
/// refuses, one fault at a time on top of a configuration that is valid.

#include <cmath>
#include <limits>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <math/rng/seed.hpp>
#include <simulation/config_validation.hpp>
#include <simulation/simulation_config.hpp>

namespace sim = mms::simulation;
namespace agents = mms::agents;
namespace math = mms::math;
namespace mm = mms::mm;

namespace {

constexpr double inf = std::numeric_limits<double>::infinity();
constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();

sim::AgentSpec noise_agent(std::string name) {
    return sim::AgentSpec{.profile = {.name = std::move(name), .initial_cash = 1000.0},
                          .timing = {},
                          .behavior =
                              agents::NoiseTraderSpec{.lambda = 1.0,
                                                      .size = sim::ExponentialSpec{.mean = 5.0},
                                                      .market_probability = 0.5,
                                                      .max_offset_ticks = 3}};
}

sim::SimulationConfig valid_config() {
    sim::SimulationConfig config{};
    config.assets = {sim::AssetSpec{.symbol = "A",
                                    .initial_mid = 100.0,
                                    .price_volatility = 0.2,
                                    .adverse_flow_size = sim::ExponentialSpec{.mean = 5.0}},
                     sim::AssetSpec{.symbol = "B",
                                    .initial_mid = 50.0,
                                    .price_volatility = 0.2,
                                    .adverse_flow_size = sim::ExponentialSpec{.mean = 5.0}}};
    config.agents = {noise_agent("alice"), noise_agent("bob")};
    config.master_seed = math::rng::Seed{1};
    config.dt = 1.0;
    config.tick_count = 10;
    return config;
}

} // namespace

TEST_CASE("A valid agent setup passes", "[simulation][config_validation]") {
    CHECK_NOTHROW(sim::validate_agent_setup(valid_config()));
}

TEST_CASE("A setup with no agent at all passes: who must be there is for the loop to say",
          "[simulation][config_validation]") {
    sim::SimulationConfig config = valid_config();
    config.agents.clear();
    CHECK_NOTHROW(sim::validate_agent_setup(config));
}

TEST_CASE("The defaults of a configuration are valid", "[simulation][config_validation]") {
    const sim::SimulationConfig config{};
    CHECK(config.book_depth == 5);
    CHECK(config.account_rules.short_collateral_rate == 1.0);
    CHECK(config.agents.empty());
    CHECK_NOTHROW(sim::validate_agent_setup(config));
    CHECK(sim::AssetSpec{}.tick_size == 0.01);
}

TEST_CASE("A tick size must be finite and positive", "[simulation][config_validation]") {
    for (const double bad : {0.0, -0.01, inf, -inf, not_a_number}) {
        sim::SimulationConfig config = valid_config();
        config.assets[1].tick_size = bad;
        CHECK_THROWS_WITH(sim::validate_agent_setup(config),
                          Catch::Matchers::ContainsSubstring("tick size of B"));
    }
    sim::SimulationConfig config = valid_config();
    config.assets[0].tick_size = 0.5;
    CHECK_NOTHROW(sim::validate_agent_setup(config));
}

TEST_CASE("The book depth must be at least one", "[simulation][config_validation]") {
    sim::SimulationConfig config = valid_config();
    config.book_depth = 0;
    CHECK_THROWS_AS(sim::validate_agent_setup(config), std::invalid_argument);
    config.book_depth = -3;
    CHECK_THROWS_AS(sim::validate_agent_setup(config), std::invalid_argument);
    config.book_depth = 1;
    CHECK_NOTHROW(sim::validate_agent_setup(config));
}

TEST_CASE("The account rules must be finite and not negative", "[simulation][config_validation]") {
    for (const double bad : {-0.1, inf, not_a_number}) {
        sim::SimulationConfig rate = valid_config();
        rate.account_rules.short_collateral_rate = bad;
        CHECK_THROWS_AS(sim::validate_agent_setup(rate), std::invalid_argument);

        sim::SimulationConfig buffer = valid_config();
        buffer.account_rules.market_order_buffer = bad;
        CHECK_THROWS_AS(sim::validate_agent_setup(buffer), std::invalid_argument);
    }
    sim::SimulationConfig zero = valid_config();
    zero.account_rules = {.short_collateral_rate = 0.0, .market_order_buffer = 0.0};
    CHECK_NOTHROW(sim::validate_agent_setup(zero));
}

TEST_CASE("Every agent needs a name, and no two share one", "[simulation][config_validation]") {
    sim::SimulationConfig unnamed = valid_config();
    unnamed.agents[1].profile.name = "";
    CHECK_THROWS_WITH(sim::validate_agent_setup(unnamed),
                      Catch::Matchers::ContainsSubstring("needs a name"));

    sim::SimulationConfig twins = valid_config();
    twins.agents[1].profile.name = "alice";
    CHECK_THROWS_WITH(sim::validate_agent_setup(twins),
                      Catch::Matchers::ContainsSubstring("share the name alice"));

    // Case matters, as it does for assets.
    sim::SimulationConfig cased = valid_config();
    cased.agents[1].profile.name = "Alice";
    CHECK_NOTHROW(sim::validate_agent_setup(cased));
}

TEST_CASE("An agent may be named like an asset: they are different things",
          "[simulation][config_validation]") {
    sim::SimulationConfig config = valid_config();
    config.agents[0].profile.name = "A";
    CHECK_NOTHROW(sim::validate_agent_setup(config));
}

TEST_CASE("A decision period must be finite and positive", "[simulation][config_validation]") {
    for (const double bad : {0.0, -1.0, inf, not_a_number}) {
        sim::SimulationConfig config = valid_config();
        config.agents[1].timing.decision_period = bad;
        CHECK_THROWS_WITH(sim::validate_agent_setup(config),
                          Catch::Matchers::ContainsSubstring("decision period of bob"));
    }
    // Shorter than dt is fine: it means every tick.
    sim::SimulationConfig config = valid_config();
    config.agents[0].timing.decision_period = 0.001;
    CHECK_NOTHROW(sim::validate_agent_setup(config));
}

TEST_CASE("The delays of an agent are validated like any other delay",
          "[simulation][config_validation]") {
    sim::SimulationConfig order = valid_config();
    order.agents[0].timing.order_delay = sim::ConstantDelaySpec{.delay = -1.0};
    CHECK_THROWS_AS(sim::validate_agent_setup(order), std::invalid_argument);

    sim::SimulationConfig info = valid_config();
    info.agents[1].timing.info_delay = sim::UniformDelaySpec{.min = 2.0, .max = 1.0};
    CHECK_THROWS_AS(sim::validate_agent_setup(info), std::invalid_argument);

    sim::SimulationConfig fine = valid_config();
    fine.agents[0].timing.order_delay = sim::ExponentialDelaySpec{.mean = 0.5};
    fine.agents[1].timing.info_delay = sim::UniformDelaySpec{.min = 0.0, .max = 1.0};
    CHECK_NOTHROW(sim::validate_agent_setup(fine));
}

TEST_CASE("Every message names the configuration", "[simulation][config_validation]") {
    sim::SimulationConfig config = valid_config();
    config.book_depth = 0;
    CHECK_THROWS_WITH(sim::validate_agent_setup(config),
                      Catch::Matchers::StartsWith("SimulationConfig: "));
}
