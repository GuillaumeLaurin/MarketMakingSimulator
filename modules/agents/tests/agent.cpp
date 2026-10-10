/// \file
/// Tests for agents/agent.hpp. The interface has one pure method, act(), and
/// three optional ones that do nothing by default. These tests drive small agents
/// through a base pointer, the way the loop will: act() appends to a buffer it does
/// not own and never clears, randomness comes from the engine it is handed (so the
/// same engine state gives the same actions), the notifications reach an override,
/// and save()/load() let a stateful agent carry on exactly where it stopped.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

#include <agents/agent.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/rng/stream.hpp>

namespace {

namespace agents = mms::agents;
namespace mm = mms::mm;
namespace rng = mms::math::rng;

static_assert(std::is_abstract_v<agents::Agent>);
static_assert(std::has_virtual_destructor_v<agents::Agent>);

rng::RngEngine engine_for(std::uint64_t seed) {
    return rng::RngHub{rng::Seed{seed}}.engine_for(
        {.subsystem = rng::Subsystem::Agent, .asset = rng::asset_key_of("test")});
}

agents::AgentView empty_view() {
    return agents::AgentView{.tick = 0,
                             .time = 0.0,
                             .account = {.cash = 0.0, .buying_power = 0.0, .positions = {}},
                             .open_orders = {},
                             .assets = {}};
}

// The least an agent can be: it overrides act() and nothing else, and does nothing.
class IdleAgent final : public agents::Agent {
public:
    void act(const agents::AgentView& /*view*/,
             rng::RngEngine& /*rng*/,
             std::vector<agents::Action>& /*out*/) override {}
};

// Draws one word from the engine it is given and trades a quantity that depends on it.
class RandomAgent final : public agents::Agent {
public:
    void act(const agents::AgentView& /*view*/,
             rng::RngEngine& rng,
             std::vector<agents::Action>& out) override {
        const auto quantity = static_cast<mm::Quantity>((rng() % 100U) + 1U);
        out.emplace_back(
            agents::SubmitMarket{.asset = 0, .side = mm::Side::Buy, .quantity = quantity});
    }
};

// Has a state: how many times it has acted. Saves it as 8 bytes.
class CountingAgent final : public agents::Agent {
public:
    void act(const agents::AgentView& /*view*/,
             rng::RngEngine& /*rng*/,
             std::vector<agents::Action>& out) override {
        ++count_;
        out.emplace_back(agents::SubmitMarket{
            .asset = 0, .side = mm::Side::Buy, .quantity = static_cast<mm::Quantity>(count_)});
    }

    [[nodiscard]] std::vector<std::byte> save() const override {
        std::vector<std::byte> bytes(sizeof(count_));
        std::memcpy(bytes.data(), &count_, sizeof(count_));
        return bytes;
    }

    void load(std::span<const std::byte> state) override {
        std::memcpy(&count_, state.data(), sizeof(count_));
    }

private:
    std::uint64_t count_ = 0;
};

// Keeps every notification it receives.
class RecordingAgent final : public agents::Agent {
public:
    void act(const agents::AgentView& /*view*/,
             rng::RngEngine& /*rng*/,
             std::vector<agents::Action>& /*out*/) override {}

    void on_fill(const agents::Fill& fill) override { fills.push_back(fill); }

    void on_reject(const agents::Action& action, agents::RejectReason reason) override {
        rejected_actions.push_back(action);
        reasons.push_back(reason);
    }

    std::vector<agents::Fill> fills;
    std::vector<agents::Action> rejected_actions;
    std::vector<agents::RejectReason> reasons;
};

std::uint64_t quantity_of(const agents::Action& action) {
    return static_cast<std::uint64_t>(std::get<agents::SubmitMarket>(action).quantity);
}

} // namespace

TEST_CASE("An agent that only overrides act() appends nothing and leaves the buffer alone",
          "[agents][agent]") {
    const std::unique_ptr<agents::Agent> agent = std::make_unique<IdleAgent>();
    rng::RngEngine engine = engine_for(1);
    std::vector<agents::Action> out;

    agent->act(empty_view(), engine, out);

    CHECK(out.empty());
}

TEST_CASE("act() appends to the buffer and never clears what the caller put there",
          "[agents][agent]") {
    const std::unique_ptr<agents::Agent> agent = std::make_unique<CountingAgent>();
    rng::RngEngine engine = engine_for(2);
    std::vector<agents::Action> out;
    out.emplace_back(agents::Cancel{.asset = 9, .id = mm::OrderId{5}});

    agent->act(empty_view(), engine, out);
    agent->act(empty_view(), engine, out);

    REQUIRE(out.size() == 3);
    CHECK(std::holds_alternative<agents::Cancel>(out[0]));
    CHECK(quantity_of(out[1]) == 1);
    CHECK(quantity_of(out[2]) == 2);
}

TEST_CASE("The defaults of the optional methods do nothing", "[agents][agent]") {
    IdleAgent agent;
    const agents::Fill fill{.asset = 0,
                            .id = mm::OrderId{1},
                            .side = mm::Side::Buy,
                            .price = 10.0,
                            .quantity = 1,
                            .aggressor = true};
    const agents::Action action = agents::Cancel{.asset = 0, .id = mm::OrderId{1}};

    agent.on_fill(fill);
    agent.on_reject(action, agents::RejectReason::SelfTrade);

    CHECK(agent.save().empty());
}

TEST_CASE("The default load() accepts any state, including an empty one", "[agents][agent]") {
    IdleAgent agent;
    const std::vector<std::byte> junk(16, std::byte{0xAB});

    agent.load({});
    agent.load(junk);

    CHECK(agent.save().empty());
}

TEST_CASE("The randomness of an agent comes from the engine it is given", "[agents][agent]") {
    RandomAgent first;
    RandomAgent second;
    rng::RngEngine engine_a = engine_for(7);
    rng::RngEngine engine_b = engine_a; // the same state, a separate copy
    std::vector<agents::Action> out_a;
    std::vector<agents::Action> out_b;

    for (int i = 0; i < 20; ++i) {
        first.act(empty_view(), engine_a, out_a);
        second.act(empty_view(), engine_b, out_b);
    }

    REQUIRE(out_a.size() == out_b.size());
    for (std::size_t i = 0; i < out_a.size(); ++i) {
        CHECK(quantity_of(out_a[i]) == quantity_of(out_b[i]));
    }
    // And it really drew: the engine moved, and the actions are not all the same.
    CHECK(engine_a == engine_b);
    CHECK(engine_a != engine_for(7));
    bool varied = false;
    for (const agents::Action& action : out_a) {
        varied = varied || quantity_of(action) != quantity_of(out_a.front());
    }
    CHECK(varied);
}

TEST_CASE("Another engine state gives other actions", "[agents][agent]") {
    RandomAgent agent;
    rng::RngEngine one = engine_for(1);
    rng::RngEngine two = engine_for(2);
    std::vector<agents::Action> out_one;
    std::vector<agents::Action> out_two;

    for (int i = 0; i < 20; ++i) {
        agent.act(empty_view(), one, out_one);
        agent.act(empty_view(), two, out_two);
    }

    bool differs = false;
    for (std::size_t i = 0; i < out_one.size(); ++i) {
        differs = differs || quantity_of(out_one[i]) != quantity_of(out_two[i]);
    }
    CHECK(differs);
}

TEST_CASE("Notifications reach an override through the base class", "[agents][agent]") {
    auto recorder = std::make_unique<RecordingAgent>();
    RecordingAgent& view = *recorder;
    const std::unique_ptr<agents::Agent> agent = std::move(recorder);

    agent->on_fill(agents::Fill{.asset = 2,
                                .id = mm::OrderId{8},
                                .side = mm::Side::Sell,
                                .price = 55.5,
                                .quantity = 4,
                                .aggressor = false});
    const agents::Action refused =
        agents::SubmitLimit{.asset = 1, .side = mm::Side::Buy, .price = 3.0, .quantity = 6};
    agent->on_reject(refused, agents::RejectReason::InsufficientCash);

    REQUIRE(view.fills.size() == 1);
    CHECK(view.fills[0].asset == 2);
    CHECK(view.fills[0].id == mm::OrderId{8});
    CHECK(view.fills[0].side == mm::Side::Sell);
    CHECK(view.fills[0].price == 55.5);
    CHECK(view.fills[0].quantity == 4);
    CHECK_FALSE(view.fills[0].aggressor);
    REQUIRE(view.rejected_actions.size() == 1);
    CHECK(std::holds_alternative<agents::SubmitLimit>(view.rejected_actions[0]));
    CHECK(view.reasons[0] == agents::RejectReason::InsufficientCash);
}

TEST_CASE("A stateful agent resumes exactly where its saved state left off", "[agents][agent]") {
    rng::RngEngine engine = engine_for(3);
    CountingAgent uninterrupted;
    CountingAgent before;
    std::vector<agents::Action> expected;
    std::vector<agents::Action> actual;

    for (int i = 0; i < 5; ++i) {
        uninterrupted.act(empty_view(), engine, expected);
        before.act(empty_view(), engine, actual);
    }
    const std::vector<std::byte> state = before.save();
    CHECK_FALSE(state.empty());

    CountingAgent after; // a fresh agent, as when a run is reopened
    after.load(state);
    for (int i = 0; i < 5; ++i) {
        uninterrupted.act(empty_view(), engine, expected);
        after.act(empty_view(), engine, actual);
    }

    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(quantity_of(actual[i]) == quantity_of(expected[i]));
    }
    CHECK(quantity_of(actual.back()) == 10);
}

TEST_CASE("save() on a stateful agent does not change it", "[agents][agent]") {
    CountingAgent agent;
    rng::RngEngine engine = engine_for(4);
    std::vector<agents::Action> out;
    agent.act(empty_view(), engine, out);

    const std::vector<std::byte> first = agent.save();
    const std::vector<std::byte> second = agent.save();

    CHECK(first == second);
}

TEST_CASE("A fill keeps the fields it is given", "[agents][agent]") {
    constexpr agents::Fill fill{.asset = 5,
                                .id = mm::OrderId{33},
                                .side = mm::Side::Buy,
                                .price = 12.75,
                                .quantity = 9,
                                .aggressor = true};

    CHECK(fill.asset == 5);
    CHECK(fill.id == mm::OrderId{33});
    CHECK(fill.side == mm::Side::Buy);
    CHECK(fill.price == 12.75);
    CHECK(fill.quantity == 9);
    CHECK(fill.aggressor);
    STATIC_REQUIRE(std::is_trivially_copyable_v<agents::Fill>);
}
