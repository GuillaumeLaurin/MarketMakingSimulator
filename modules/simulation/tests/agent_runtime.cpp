/// \file
/// Tests for simulation/agent_runtime.hpp, driven by a scripted agent that asks for whatever
/// the test tells it to and remembers what it was shown and told. What is checked is what
/// the loop would see: the view the agent gets, the messages that come out with their due
/// times, the refusals, and the state of the account and the order list after each step of an
/// order's life.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include <agents/agent.hpp>
#include <agents/noise_trader.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/pnl.hpp>

#include <math/rng/engine.hpp>
#include <math/rng/seed.hpp>
#include <math/rng/stream.hpp>
#include <simulation/agent_runtime.hpp>
#include <simulation/latency.hpp>
#include <simulation/size_spec.hpp>

#include "scripted.hpp"

namespace sim = mms::simulation;
namespace agents = mms::agents;
namespace math = mms::math;
namespace mm = mms::mm;

namespace {

using mm::Side;

using sim::tests::Scripted;

const std::vector<mm::LedgerAsset> two_assets = {{.initial_mid = 100.0}, {.initial_mid = 50.0}};

math::rng::RngEngine stream(math::rng::Subsystem subsystem, std::uint64_t seed = 7) {
    const math::rng::RngHub hub(math::rng::Seed{seed});
    return hub.engine_for({.subsystem = subsystem, .asset = math::rng::asset_key_of("agent")});
}

sim::AgentSpec spec_with(sim::AgentTiming timing, double cash = 1000.0) {
    return sim::AgentSpec{.profile = {.name = "agent", .initial_cash = cash},
                          .timing = timing,
                          .behavior =
                              agents::NoiseTraderSpec{.lambda = 1.0,
                                                      .size = sim::ExponentialSpec{.mean = 5.0},
                                                      .market_probability = 0.5,
                                                      .max_offset_ticks = 3}};
}

/// A runtime around a Scripted agent, which the test keeps a pointer to.
struct Rig {
    explicit Rig(sim::AgentTiming timing = {}, double cash = 1000.0, std::uint64_t seed = 7) {
        auto agent = std::make_unique<Scripted>();
        script = agent.get();
        runtime =
            std::make_unique<sim::AgentRuntime>(3,
                                                spec_with(timing, cash),
                                                sim::AccountRules{},
                                                two_assets,
                                                stream(math::rng::Subsystem::Agent, seed),
                                                stream(math::rng::Subsystem::AgentLatency, seed),
                                                std::move(agent));
    }

    sim::ActResult act(std::int64_t tick, double time) { return runtime->act(tick, time, markets); }

    Scripted* script = nullptr;
    std::unique_ptr<sim::AgentRuntime> runtime;
    std::vector<agents::AssetView> markets = std::vector<agents::AssetView>(2);
};

agents::SubmitLimit buy(mm::AssetId asset, mm::Price price, mm::Quantity quantity) {
    return agents::SubmitLimit{
        .asset = asset, .side = Side::Buy, .price = price, .quantity = quantity};
}

const sim::SubmitArrival& as_submit(const sim::Scheduled& scheduled) {
    return std::get<sim::SubmitArrival>(scheduled.event);
}

} // namespace

TEST_CASE("The runtime is built from a spec and carries its id, spec and a live agent",
          "[simulation][agent_runtime]") {
    const sim::AgentRuntime runtime(5,
                                    spec_with({}),
                                    sim::AccountRules{},
                                    two_assets,
                                    stream(math::rng::Subsystem::Agent),
                                    stream(math::rng::Subsystem::AgentLatency));

    CHECK(runtime.id() == 5);
    CHECK(runtime.spec().profile.name == "agent");
    CHECK(runtime.account().cash() == 1000.0);
    CHECK(runtime.orders().size() == 0);
    CHECK(dynamic_cast<const agents::NoiseTraderAgent*>(&runtime.agent()) != nullptr);
}

TEST_CASE("A bad behavior or a bad profile is refused at construction",
          "[simulation][agent_runtime]") {
    sim::AgentSpec bad_behavior = spec_with({});
    std::get<agents::NoiseTraderSpec>(bad_behavior.behavior).lambda = -1.0;
    CHECK_THROWS_AS(sim::AgentRuntime(0,
                                      bad_behavior,
                                      sim::AccountRules{},
                                      two_assets,
                                      stream(math::rng::Subsystem::Agent),
                                      stream(math::rng::Subsystem::AgentLatency)),
                    std::invalid_argument);

    CHECK_THROWS_AS(sim::AgentRuntime(0,
                                      spec_with({}, -5.0),
                                      sim::AccountRules{},
                                      two_assets,
                                      stream(math::rng::Subsystem::Agent),
                                      stream(math::rng::Subsystem::AgentLatency)),
                    std::invalid_argument);

    CHECK_THROWS_AS(sim::AgentRuntime(0,
                                      spec_with({}),
                                      sim::AccountRules{},
                                      two_assets,
                                      stream(math::rng::Subsystem::Agent),
                                      stream(math::rng::Subsystem::AgentLatency),
                                      nullptr),
                    std::invalid_argument);
}

TEST_CASE("The agent acts at once, then once per decision period", "[simulation][agent_runtime]") {
    Rig rig({.decision_period = 2.0});

    CHECK(rig.runtime->due(0.0));
    CHECK(rig.runtime->due(1.0));

    static_cast<void>(rig.act(1, 1.0));

    CHECK_FALSE(rig.runtime->due(1.0));
    CHECK_FALSE(rig.runtime->due(2.9));
    CHECK(rig.runtime->due(3.0));
    CHECK(rig.runtime->due(10.0));
}

TEST_CASE("The clock counts from when the agent last acted, not from the tick it was due",
          "[simulation][agent_runtime]") {
    Rig rig({.decision_period = 1.5});

    static_cast<void>(rig.act(1, 1.0));
    CHECK_FALSE(rig.runtime->due(2.0));
    CHECK(rig.runtime->due(3.0)); // due from 2.5, so seen at tick 3

    static_cast<void>(rig.act(3, 3.0));
    CHECK_FALSE(rig.runtime->due(4.0));
    CHECK(rig.runtime->due(4.5));
}

TEST_CASE("Rounding does not make an agent miss a tick", "[simulation][agent_runtime]") {
    Rig rig({.decision_period = 0.3});

    static_cast<void>(rig.act(1, 0.1 + 0.2)); // 0.30000000000000004
    // 0.30000000000000004 + 0.3 is 0.6000000000000001, which 0.6 is a hair under.
    CHECK(rig.runtime->due(0.6));
    CHECK_FALSE(rig.runtime->due(0.59));
}

TEST_CASE("The view shows the tick, the time, the account, the orders and the markets",
          "[simulation][agent_runtime]") {
    Rig rig({}, 1000.0);
    rig.markets[0].tick_size = 0.05;

    static_cast<void>(rig.act(4, 4.0));

    CHECK(rig.script->acts == 1);
    CHECK(rig.script->last_tick == 4);
    CHECK(rig.script->last_time == 4.0);
    CHECK(rig.script->last_cash == 1000.0);
    CHECK(rig.script->last_buying_power == 1000.0);
    CHECK(rig.script->last_positions == std::vector<mm::Quantity>{0, 0});
    CHECK(rig.script->last_orders.empty());
    CHECK(rig.script->last_asset_count == 2);
    CHECK(rig.script->last_factors.empty());
}

TEST_CASE("The first elapsed time is the time since the start, then the time between acts",
          "[simulation][agent_runtime]") {
    Rig rig;

    static_cast<void>(rig.act(3, 3.0));
    CHECK(rig.script->last_elapsed == 3.0);

    static_cast<void>(rig.act(7, 7.5));
    CHECK(rig.script->last_elapsed == 4.5);
}

TEST_CASE("The agent is given its own decision stream, which goes on from one act to the next",
          "[simulation][agent_runtime]") {
    Rig first(sim::AgentTiming{}, 1000.0, 7);
    Rig second(sim::AgentTiming{}, 1000.0, 7);
    Rig other(sim::AgentTiming{}, 1000.0, 8);

    static_cast<void>(first.act(1, 1.0));
    static_cast<void>(first.act(2, 2.0));
    static_cast<void>(second.act(1, 1.0));
    static_cast<void>(second.act(2, 2.0));
    static_cast<void>(other.act(1, 1.0));

    REQUIRE(first.script->draws.size() == 2);
    CHECK(first.script->draws == second.script->draws);
    CHECK(first.script->draws[0] != first.script->draws[1]);
    CHECK(other.script->draws[0] != first.script->draws[0]);

    // And it is the Agent stream, not the latency one.
    math::rng::RngEngine expected = stream(math::rng::Subsystem::Agent, 7);
    CHECK(first.script->draws[0] == expected());
}

TEST_CASE("An accepted limit order is scheduled after its delay and listed as on its way",
          "[simulation][agent_runtime]") {
    Rig rig({.order_delay = sim::ConstantDelaySpec{.delay = 0.5}});
    rig.script->to_ask = {buy(1, 49.0, 10)};

    const sim::ActResult result = rig.act(1, 2.0);

    REQUIRE(result.scheduled.size() == 1);
    CHECK(result.refused.empty());
    CHECK(result.scheduled[0].due == 2.5);
    const sim::SubmitArrival& arrival = as_submit(result.scheduled[0]);
    CHECK(arrival.agent == 3);
    const auto* limit = std::get_if<agents::SubmitLimit>(&arrival.order);
    REQUIRE(limit != nullptr);
    CHECK(limit->asset == 1);
    CHECK(limit->price == 49.0);
    CHECK(limit->quantity == 10);

    REQUIRE(rig.runtime->orders().size() == 1);
    const agents::OpenOrder& listed = rig.runtime->orders().orders()[0];
    CHECK(listed.state == agents::OrderState::Submitting);
    CHECK(listed.asset == 1);
    CHECK(listed.price == 49.0);
    CHECK(listed.remaining == 10);
    CHECK(rig.runtime->account().buying_power() == 1000.0 - 490.0);
    CHECK(rig.runtime->account().pending_buy(1) == 10);
}

TEST_CASE("An accepted market order is scheduled and listed with no price",
          "[simulation][agent_runtime]") {
    Rig rig;
    rig.script->to_ask = {agents::SubmitMarket{.asset = 0, .side = Side::Sell, .quantity = 4}};
    // A sale needs stock or the right to short; this agent has neither.
    const sim::ActResult refused = rig.act(1, 1.0);
    CHECK(refused.scheduled.empty());

    sim::AgentSpec spec = spec_with({});
    spec.profile.can_short = true;
    Scripted* script = nullptr;
    auto agent = std::make_unique<Scripted>();
    script = agent.get();
    sim::AgentRuntime runtime(0,
                              spec,
                              sim::AccountRules{},
                              two_assets,
                              stream(math::rng::Subsystem::Agent),
                              stream(math::rng::Subsystem::AgentLatency),
                              std::move(agent));
    script->to_ask = {agents::SubmitMarket{.asset = 0, .side = Side::Sell, .quantity = 4}};

    const std::vector<agents::AssetView> markets(2);
    const sim::ActResult result = runtime.act(1, 1.0, markets);

    REQUIRE(result.scheduled.size() == 1);
    const auto* market = std::get_if<agents::SubmitMarket>(&as_submit(result.scheduled[0]).order);
    REQUIRE(market != nullptr);
    CHECK(market->quantity == 4);
    REQUIRE(runtime.orders().size() == 1);
    CHECK(runtime.orders().orders()[0].price == 0.0);
    CHECK(runtime.orders().orders()[0].state == agents::OrderState::Submitting);
}

TEST_CASE("A refused action is returned, reported to the agent, and holds nothing",
          "[simulation][agent_runtime]") {
    Rig rig({}, 100.0);
    rig.script->to_ask = {buy(0, 100.0, 5)}; // 500 against 100 of cash

    const sim::ActResult result = rig.act(1, 1.0);

    CHECK(result.scheduled.empty());
    REQUIRE(result.refused.size() == 1);
    CHECK(result.refused[0].reason == agents::RejectReason::InsufficientCash);
    CHECK(std::get<agents::SubmitLimit>(result.refused[0].action).quantity == 5);
    REQUIRE(rig.script->rejects.size() == 1);
    CHECK(rig.script->rejects[0].second == agents::RejectReason::InsufficientCash);
    CHECK(rig.runtime->orders().size() == 0);
    CHECK(rig.runtime->account().pending_count() == 0);
    CHECK(rig.runtime->account().buying_power() == 100.0);
}

TEST_CASE(
    "Actions are judged one after the other: the first one's reserve counts against the second",
    "[simulation][agent_runtime]") {
    Rig rig({}, 1000.0);
    rig.script->to_ask = {buy(0, 10.0, 60), buy(1, 10.0, 60), buy(0, 10.0, 30)};

    const sim::ActResult result = rig.act(1, 1.0);

    // 600 reserved by the first leaves 400: the second (600) is refused, the third (300) fits.
    REQUIRE(result.scheduled.size() == 2);
    REQUIRE(result.refused.size() == 1);
    CHECK(std::get<agents::SubmitLimit>(result.refused[0].action).asset == 1);
    CHECK(as_submit(result.scheduled[1]).ticket != as_submit(result.scheduled[0]).ticket);
    CHECK(rig.runtime->orders().size() == 2);
}

TEST_CASE("A delay is drawn for every action asked for, so a refusal never shifts a later draw",
          "[simulation][agent_runtime]") {
    const sim::AgentTiming timing{.order_delay = sim::UniformDelaySpec{.min = 0.0, .max = 10.0}};

    Rig with_refusal(timing, 100.0);
    with_refusal.script->to_ask = {buy(0, 100.0, 5), buy(0, 1.0, 1)}; // the first is refused
    const sim::ActResult first = with_refusal.act(1, 0.0);

    Rig without(timing, 100.0);
    without.script->to_ask = {buy(0, 1.0, 1), buy(0, 1.0, 1)}; // both accepted
    const sim::ActResult second = without.act(1, 0.0);

    REQUIRE(first.scheduled.size() == 1);
    REQUIRE(second.scheduled.size() == 2);
    CHECK(first.scheduled[0].due == second.scheduled[1].due);
    CHECK(second.scheduled[0].due != second.scheduled[1].due);
}

TEST_CASE("The delay stream is the agent's own latency stream", "[simulation][agent_runtime]") {
    Rig rig({.order_delay = sim::UniformDelaySpec{.min = 0.0, .max = 10.0}});
    rig.script->to_ask = {buy(0, 1.0, 1)};

    const sim::ActResult result = rig.act(1, 0.0);

    math::rng::RngEngine expected = stream(math::rng::Subsystem::AgentLatency);
    REQUIRE(result.scheduled.size() == 1);
    CHECK(result.scheduled[0].due ==
          sim::draw_delay(rig.runtime->spec().timing.order_delay, expected));
}

TEST_CASE("An order that arrives rests, and can then be cancelled", "[simulation][agent_runtime]") {
    Rig rig({.order_delay = sim::ConstantDelaySpec{.delay = 1.0}});
    rig.script->to_ask = {buy(0, 10.0, 5)};
    const sim::SubmitArrival arrival = as_submit(rig.act(1, 1.0).scheduled[0]);

    rig.runtime->on_arrived(arrival.ticket, mm::OrderId{42});
    CHECK(rig.runtime->orders().orders()[0].state == agents::OrderState::Resting);
    CHECK(rig.runtime->orders().orders()[0].id == mm::OrderId{42});

    rig.script->to_ask = {agents::Cancel{.asset = 0, .id = mm::OrderId{42}}};
    const sim::ActResult result = rig.act(2, 2.0);

    REQUIRE(result.scheduled.size() == 1);
    CHECK(result.scheduled[0].due == 3.0);
    const auto* cancel = std::get_if<sim::CancelArrival>(&result.scheduled[0].event);
    REQUIRE(cancel != nullptr);
    CHECK(cancel->agent == 3);
    CHECK(cancel->asset == 0);
    CHECK(cancel->id == mm::OrderId{42});
    CHECK(rig.runtime->orders().orders()[0].state == agents::OrderState::Cancelling);
    // A cancel holds nothing back and frees nothing until it goes through.
    CHECK(rig.runtime->account().pending_buy(0) == 5);
}

TEST_CASE("A cancel of an order that is not resting is dropped without a word",
          "[simulation][agent_runtime]") {
    Rig rig;
    rig.script->to_ask = {buy(0, 10.0, 5)};
    const sim::SubmitArrival arrival = as_submit(rig.act(1, 1.0).scheduled[0]);

    // Unknown order.
    rig.script->to_ask = {agents::Cancel{.asset = 0, .id = mm::OrderId{99}}};
    sim::ActResult result = rig.act(2, 2.0);
    CHECK(result.scheduled.empty());
    CHECK(result.refused.empty());
    CHECK(rig.script->rejects.empty());

    // Still on its way: it has no id yet.
    rig.script->to_ask = {agents::Cancel{.asset = 0, .id = mm::OrderId{0}}};
    CHECK(rig.act(3, 3.0).scheduled.empty());

    // Already being cancelled: the second cancel is dropped.
    rig.runtime->on_arrived(arrival.ticket, mm::OrderId{7});
    rig.script->to_ask = {agents::Cancel{.asset = 0, .id = mm::OrderId{7}}};
    CHECK(rig.act(4, 4.0).scheduled.size() == 1);
    CHECK(rig.act(5, 5.0).scheduled.empty());
}

TEST_CASE("A fill settles the account, shrinks the order and tells the agent",
          "[simulation][agent_runtime]") {
    Rig rig({}, 1000.0);
    rig.script->to_ask = {buy(0, 10.0, 8)};
    const sim::SubmitArrival arrival = as_submit(rig.act(1, 1.0).scheduled[0]);
    rig.runtime->on_arrived(arrival.ticket, mm::OrderId{1});

    const agents::Fill fill{.asset = 0,
                            .id = mm::OrderId{1},
                            .side = Side::Buy,
                            .price = 9.5,
                            .quantity = 3,
                            .aggressor = false};
    rig.runtime->on_fill(arrival.ticket, fill);

    CHECK(rig.runtime->account().cash() == 1000.0 - 28.5);
    CHECK(rig.runtime->account().ledger().quantities() == std::vector<mm::Quantity>{3, 0});
    REQUIRE(rig.runtime->orders().size() == 1);
    CHECK(rig.runtime->orders().orders()[0].remaining == 5);
    REQUIRE(rig.script->fills.size() == 1);
    CHECK(rig.script->fills[0].quantity == 3);
    CHECK(rig.script->fills[0].price == 9.5);
    CHECK(rig.runtime->account().pending_buy(0) == 5);

    // What the next view shows.
    rig.script->to_ask.clear();
    static_cast<void>(rig.act(2, 2.0));
    CHECK(rig.script->last_positions == std::vector<mm::Quantity>{3, 0});
    CHECK(rig.script->last_cash == 1000.0 - 28.5);
    REQUIRE(rig.script->last_orders.size() == 1);
    CHECK(rig.script->last_orders[0].remaining == 5);

    // The rest fills: the order is gone and so is its ticket.
    rig.runtime->on_fill(arrival.ticket,
                         {.asset = 0,
                          .id = mm::OrderId{1},
                          .side = Side::Buy,
                          .price = 10.0,
                          .quantity = 5,
                          .aggressor = false});
    CHECK(rig.runtime->orders().size() == 0);
    CHECK(rig.runtime->account().pending_count() == 0);
}

TEST_CASE("A fill larger than what is left is refused", "[simulation][agent_runtime]") {
    Rig rig;
    rig.script->to_ask = {buy(0, 10.0, 2)};
    const sim::SubmitArrival arrival = as_submit(rig.act(1, 1.0).scheduled[0]);

    CHECK_THROWS_AS(rig.runtime->on_fill(arrival.ticket,
                                         {.asset = 0,
                                          .id = mm::OrderId{1},
                                          .side = Side::Buy,
                                          .price = 10.0,
                                          .quantity = 3,
                                          .aggressor = false}),
                    std::invalid_argument);
    CHECK(rig.script->fills.empty());
}

TEST_CASE("An order that ends without trading frees what it held back",
          "[simulation][agent_runtime]") {
    Rig rig({}, 1000.0);
    rig.script->to_ask = {buy(0, 10.0, 50)};
    const sim::SubmitArrival arrival = as_submit(rig.act(1, 1.0).scheduled[0]);
    REQUIRE(rig.runtime->account().buying_power() == 500.0);

    CHECK(rig.runtime->on_done(arrival.ticket));

    CHECK(rig.runtime->account().buying_power() == 1000.0);
    CHECK(rig.runtime->orders().size() == 0);
    CHECK_FALSE(rig.runtime->on_done(arrival.ticket)); // already closed: not an error
}

TEST_CASE("Factor moves add up until the agent acts, then start again from zero",
          "[simulation][agent_runtime]") {
    Rig rig;
    const std::vector<double> first = {0.5, -0.25};
    const std::vector<double> second = {0.25, 0.5};

    rig.runtime->add_factors(first);
    rig.runtime->add_factors(second);
    static_cast<void>(rig.act(1, 1.0));
    CHECK(rig.script->last_factors == std::vector<double>{0.75, 0.25});

    static_cast<void>(rig.act(2, 2.0));
    CHECK(rig.script->last_factors == std::vector<double>{0.0, 0.0});

    rig.runtime->add_factors(second);
    static_cast<void>(rig.act(3, 3.0));
    CHECK(rig.script->last_factors == std::vector<double>{0.25, 0.5});
}

TEST_CASE("The number of factors is fixed by the first call", "[simulation][agent_runtime]") {
    Rig rig;
    const std::vector<double> two = {1.0, 2.0};
    const std::vector<double> three = {1.0, 2.0, 3.0};

    rig.runtime->add_factors(two);
    CHECK_THROWS_AS(rig.runtime->add_factors(three), std::invalid_argument);
    CHECK_THROWS_AS(rig.runtime->add_factors(std::span<const double>{}), std::invalid_argument);
}

TEST_CASE("A process with no factors adds nothing and the agent sees none",
          "[simulation][agent_runtime]") {
    Rig rig;

    rig.runtime->add_factors(std::span<const double>{});
    static_cast<void>(rig.act(1, 1.0));

    CHECK(rig.script->last_factors.empty());
}

TEST_CASE("A cancel names the side of the order it cancels", "[simulation][agent_runtime]") {
    // A sale needs the right to short.
    sim::AgentSpec spec = spec_with({});
    spec.profile.can_short = true;
    auto agent = std::make_unique<Scripted>();
    Scripted* script = agent.get();
    sim::AgentRuntime runtime(0,
                              spec,
                              sim::AccountRules{},
                              two_assets,
                              stream(math::rng::Subsystem::Agent),
                              stream(math::rng::Subsystem::AgentLatency),
                              std::move(agent));
    const std::vector<agents::AssetView> markets(2);
    script->to_ask = {
        agents::SubmitLimit{.asset = 1, .side = Side::Sell, .price = 55.0, .quantity = 3}};
    const sim::SubmitArrival arrival = as_submit(runtime.act(1, 1.0, markets).scheduled[0]);
    runtime.on_arrived(arrival.ticket, mm::OrderId{5});

    script->to_ask = {agents::Cancel{.asset = 1, .id = mm::OrderId{5}}};
    const sim::ActResult result = runtime.act(2, 2.0, markets);

    REQUIRE(result.scheduled.size() == 1);
    CHECK(std::get<sim::CancelArrival>(result.scheduled[0].event).side == Side::Sell);
}

TEST_CASE("A replacement never reaches the exchange before the cancel of the order it replaces",
          "[simulation][agent_runtime]") {
    // The cancel is slow, the new order would be fast: it is held back to the cancel's time.
    // Over many seeds with a wide delay the draws are in either order.
    int held_back = 0;
    int on_its_own = 0;
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        Rig rig({.order_delay = sim::UniformDelaySpec{.min = 0.0, .max = 10.0}}, 1000.0, seed);
        rig.script->to_ask = {buy(0, 10.0, 5)};
        const sim::SubmitArrival old_order = as_submit(rig.act(1, 0.0).scheduled[0]);
        rig.runtime->on_arrived(old_order.ticket, mm::OrderId{9});

        rig.script->to_ask = {agents::Cancel{.asset = 0, .id = mm::OrderId{9}}, buy(0, 11.0, 5)};
        const sim::ActResult result = rig.act(2, 100.0);

        REQUIRE(result.scheduled.size() == 2);
        const double cancel_due = result.scheduled[0].due;
        const double submit_due = result.scheduled[1].due;
        CHECK(submit_due >= cancel_due);
        if (submit_due == cancel_due) {
            ++held_back;
        } else {
            ++on_its_own;
        }
    }
    CHECK(held_back > 0);  // the delay of the new order really was drawn shorter at times
    CHECK(on_its_own > 0); // and a longer one is left as it was drawn
}

TEST_CASE("Only a cancel on the same asset and side holds a new order back",
          "[simulation][agent_runtime]") {
    // Same seed, same draws: the only difference is the asset (or the side) of the new order.
    const sim::AgentTiming timing{.order_delay = sim::UniformDelaySpec{.min = 0.0, .max = 10.0}};
    int different_asset_earlier = 0;
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        Rig rig(timing, 1000.0, seed);
        rig.script->to_ask = {buy(0, 10.0, 5)};
        const sim::SubmitArrival old_order = as_submit(rig.act(1, 0.0).scheduled[0]);
        rig.runtime->on_arrived(old_order.ticket, mm::OrderId{9});

        rig.script->to_ask = {agents::Cancel{.asset = 0, .id = mm::OrderId{9}}, buy(1, 10.0, 5)};
        const sim::ActResult result = rig.act(2, 100.0);

        REQUIRE(result.scheduled.size() == 2);
        if (result.scheduled[1].due < result.scheduled[0].due) {
            ++different_asset_earlier;
        }
    }
    CHECK(different_asset_earlier > 0);
}

TEST_CASE("A cancel on one side does not hold back a new order on the other side",
          "[simulation][agent_runtime]") {
    int other_side_earlier = 0;
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        sim::AgentSpec spec =
            spec_with({.order_delay = sim::UniformDelaySpec{.min = 0.0, .max = 10.0}});
        spec.profile.can_short = true;
        auto agent = std::make_unique<Scripted>();
        Scripted* script = agent.get();
        sim::AgentRuntime runtime(0,
                                  spec,
                                  sim::AccountRules{},
                                  two_assets,
                                  stream(math::rng::Subsystem::Agent, seed),
                                  stream(math::rng::Subsystem::AgentLatency, seed),
                                  std::move(agent));
        const std::vector<agents::AssetView> markets(2);
        script->to_ask = {buy(0, 10.0, 5)};
        const sim::SubmitArrival old_bid = as_submit(runtime.act(1, 0.0, markets).scheduled[0]);
        runtime.on_arrived(old_bid.ticket, mm::OrderId{9});

        script->to_ask = {
            agents::Cancel{.asset = 0, .id = mm::OrderId{9}},
            agents::SubmitLimit{.asset = 0, .side = Side::Sell, .price = 12.0, .quantity = 5}};
        const sim::ActResult result = runtime.act(2, 100.0, markets);

        REQUIRE(result.scheduled.size() == 2);
        if (result.scheduled[1].due < result.scheduled[0].due) {
            ++other_side_earlier;
        }
    }
    CHECK(other_side_earlier > 0);
}
