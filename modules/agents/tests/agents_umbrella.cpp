/// \file
/// <agents/agents.hpp> must expose the whole interface on its own: an agent can be
/// written, an action built, a view read and a reject reason named from this one
/// include.

#include <vector>

#include <agents/agents.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/rng/stream.hpp>

namespace {

class OneShotAgent final : public mms::agents::Agent {
public:
    void act(const mms::agents::AgentView& view,
             mms::math::rng::RngEngine& /*rng*/,
             std::vector<mms::agents::Action>& out) override {
        if (view.tick == 0) {
            out.emplace_back(mms::agents::SubmitLimit{
                .asset = 0, .side = mms::mm::Side::Buy, .price = 10.0, .quantity = 1});
        }
    }
};

} // namespace

TEST_CASE("<agents/agents.hpp> exposes the whole agent interface", "[agents][umbrella]") {
    using namespace mms::agents;

    OneShotAgent agent;
    const AgentView view{.tick = 0,
                         .time = 0.0,
                         .account = {.cash = 1.0, .buying_power = 1.0, .positions = {}},
                         .open_orders = {},
                         .assets = {}};
    mms::math::rng::RngEngine engine = mms::math::rng::RngHub{mms::math::rng::Seed{1}}.engine_for(
        {.subsystem = mms::math::rng::Subsystem::Agent, .asset = mms::math::rng::no_asset});
    std::vector<Action> out;

    agent.act(view, engine, out);

    REQUIRE(out.size() == 1);
    CHECK(std::holds_alternative<SubmitLimit>(out[0]));
    CHECK(to_string(RejectReason::PositionLimit) == "position limit");
    const BookLevel level{.price = 1.0, .quantity = 1};
    const TradePrint print{.price = 1.0, .quantity = 1, .aggressor = mms::mm::Side::Buy};
    const OpenOrder order{.asset = 0,
                          .id = mms::mm::OrderId{1},
                          .side = mms::mm::Side::Buy,
                          .price = 1.0,
                          .remaining = 1};
    const Fill fill{.asset = 0,
                    .id = mms::mm::OrderId{1},
                    .side = mms::mm::Side::Buy,
                    .price = 1.0,
                    .quantity = 1,
                    .aggressor = false};
    CHECK(level.quantity == print.quantity);
    CHECK(order.remaining == fill.quantity);
}
