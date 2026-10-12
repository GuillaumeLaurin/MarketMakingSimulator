/// \file
/// Tests for simulation/adverse_flow.hpp: generate_adverse_flow, the whole-tick flow made of
/// the three draws of agents/flow.hpp (which are tested there). What matters here is that it
/// is exactly the documented composition of those draws, in the documented order, and that it
/// validates everything before it draws anything.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/types.hpp>

#include <math/dist/exponential.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/seed.hpp>
#include <math/rng/stream.hpp>
#include <simulation/adverse_flow.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/size_spec.hpp>

namespace sim = mms::simulation;
namespace math = mms::math;
namespace mm = mms::mm;

namespace {

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

math::rng::RngEngine fresh_engine(std::uint64_t seed = 1) {
    const math::rng::RngHub hub{math::rng::Seed{seed}};
    return hub.engine_for(
        {.subsystem = math::rng::Subsystem::AdverseFlow, .asset = math::rng::AssetKey{0}});
}

/// True if `engine` has not consumed a single word since `reference` was copied.
bool untouched(math::rng::RngEngine engine, math::rng::RngEngine reference) {
    return engine() == reference();
}

sim::AssetSpec flow_asset(double lambda, double correlation, sim::DistributionSpec size) {
    return sim::AssetSpec{.symbol = "FLOW",
                          .initial_mid = 100.0,
                          .price_volatility = 0.2,
                          .factor_model = std::nullopt,
                          .adverse_flow_lambda = lambda,
                          .adverse_flow_direction_correlation = correlation,
                          .adverse_flow_size = size};
}

} // namespace

// ---------------------------------------------------------- whole-tick flow

TEST_CASE("A zero rate generates no orders", "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();

    const auto orders = sim::generate_adverse_flow(
        flow_asset(0.0, 0.5, sim::UniformIntSpec{.min = 1, .max = 5}), 1.0, 1.0, engine);

    CHECK(orders.empty());
}

TEST_CASE("Every generated order is a Market IOC order of at least one share",
          "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine(23);
    int generated = 0;

    for (int tick = 0; tick < 200; ++tick) {
        const auto orders = sim::generate_adverse_flow(
            flow_asset(4.0, 0.5, sim::ExponentialSpec{.mean = 6.0}), 1.0, 1.0, engine);
        for (const mm::MarketOrder& order : orders) {
            CHECK(order.time_in_force == mm::MarketTimeInForce::IOC);
            CHECK(order.quantity >= 1);
            ++generated;
        }
    }
    CHECK(generated > 0);
}

TEST_CASE("The number of orders is the arrival count of the same engine state",
          "[simulation][adverse_flow]") {
    const sim::AssetSpec asset = flow_asset(6.0, 0.3, sim::UniformIntSpec{.min = 1, .max = 9});

    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        math::rng::RngEngine engine = fresh_engine(seed);
        math::rng::RngEngine replay = engine;

        const auto orders = sim::generate_adverse_flow(asset, 1.0, 1.0, engine);

        CHECK(static_cast<std::int64_t>(orders.size()) ==
              sim::draw_arrival_count(6.0, 1.0, replay));
    }
}

TEST_CASE("generate_adverse_flow is exactly the documented composition of the draws",
          "[simulation][adverse_flow]") {
    // Arrival count first, then for each occurrence its direction, then its
    // size. Replaying that order by hand must reproduce every order.
    const sim::AssetSpec asset = flow_asset(8.0, 0.4, sim::UniformIntSpec{.min = 1, .max = 50});
    constexpr double last_move = -0.7;

    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        math::rng::RngEngine engine = fresh_engine(seed);
        math::rng::RngEngine replay = engine;

        const auto orders = sim::generate_adverse_flow(asset, last_move, 1.0, engine);

        const std::int64_t count = sim::draw_arrival_count(8.0, 1.0, replay);
        REQUIRE(std::cmp_equal(orders.size(), count));
        for (const mm::MarketOrder& order : orders) {
            const mm::Side side = sim::draw_direction(0.4, last_move, replay);
            const mm::Quantity quantity = sim::draw_size(asset.adverse_flow_size, replay);
            CHECK(order.side == side);
            CHECK(order.quantity == quantity);
        }
        CHECK(untouched(engine, replay)); // and not one word more or less
    }
}

TEST_CASE("The same engine state gives the same orders", "[simulation][adverse_flow]") {
    const sim::AssetSpec asset = flow_asset(5.0, 0.6, sim::ExponentialSpec{.mean = 4.0});
    math::rng::RngEngine first = fresh_engine(31);
    math::rng::RngEngine second = fresh_engine(31);

    for (int tick = 0; tick < 50; ++tick) {
        const auto a = sim::generate_adverse_flow(asset, 0.2, 1.0, first);
        const auto b = sim::generate_adverse_flow(asset, 0.2, 1.0, second);
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK(a[i].side == b[i].side);
            CHECK(a[i].quantity == b[i].quantity);
        }
    }
}

TEST_CASE("With full correlation every order follows the last move", "[simulation][adverse_flow]") {
    const sim::AssetSpec asset = flow_asset(5.0, 1.0, sim::UniformIntSpec{.min = 1, .max = 5});
    math::rng::RngEngine engine = fresh_engine(37);
    int checked = 0;

    for (int tick = 0; tick < 100; ++tick) {
        for (const mm::MarketOrder& order : sim::generate_adverse_flow(asset, 2.0, 1.0, engine)) {
            CHECK(order.side == mm::Side::Buy);
            ++checked;
        }
        for (const mm::MarketOrder& order : sim::generate_adverse_flow(asset, -2.0, 1.0, engine)) {
            CHECK(order.side == mm::Side::Sell);
            ++checked;
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("The asset's own size spec is the one used", "[simulation][adverse_flow]") {
    const sim::AssetSpec asset = flow_asset(6.0, 0.0, sim::UniformIntSpec{.min = 4, .max = 4});
    math::rng::RngEngine engine = fresh_engine(41);
    int checked = 0;

    for (int tick = 0; tick < 100; ++tick) {
        for (const mm::MarketOrder& order : sim::generate_adverse_flow(asset, 0.0, 1.0, engine)) {
            CHECK(order.quantity == 4);
            ++checked;
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("generate_adverse_flow validates everything up front, even at a zero rate",
          "[simulation][adverse_flow]") {
    math::rng::RngEngine engine = fresh_engine();
    const math::rng::RngEngine before = engine;
    const sim::DistributionSpec fine = sim::UniformIntSpec{.min = 1, .max = 5};

    // Zero rate: the loop would never run, so these only fail if checked first.
    CHECK_THROWS_AS(sim::generate_adverse_flow(flow_asset(0.0, 1.5, fine), 1.0, 1.0, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(sim::generate_adverse_flow(flow_asset(0.0, -0.1, fine), 1.0, 1.0, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        sim::generate_adverse_flow(
            flow_asset(0.0, 0.5, sim::UniformIntSpec{.min = 0, .max = 5}), 1.0, 1.0, engine),
        std::invalid_argument);
    CHECK_THROWS_AS(sim::generate_adverse_flow(
                        flow_asset(0.0, 0.5, sim::ExponentialSpec{.mean = -2.0}), 1.0, 1.0, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(sim::generate_adverse_flow(flow_asset(0.0, 0.5, fine), quiet_nan, 1.0, engine),
                    std::invalid_argument);

    // And the rate and the time step themselves.
    CHECK_THROWS_AS(sim::generate_adverse_flow(flow_asset(-1.0, 0.5, fine), 1.0, 1.0, engine),
                    std::invalid_argument);
    CHECK_THROWS_AS(sim::generate_adverse_flow(flow_asset(1.0, 0.5, fine), 1.0, 0.0, engine),
                    std::invalid_argument);

    CHECK(untouched(engine, before)); // not one of those rejections moved the stream
}
