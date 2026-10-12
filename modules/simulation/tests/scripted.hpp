#ifndef MMS_SIMULATION_TESTS_SCRIPTED_HPP
#define MMS_SIMULATION_TESTS_SCRIPTED_HPP

/// \file
/// Test scaffolding: an agent that asks for whatever the test tells it to and remembers what
/// it was shown and told.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <agents/agent.hpp>
#include <mm/types.hpp>

#include <math/rng/engine.hpp>

namespace mms::simulation::tests {

/// Asks for what it has been given and keeps what it saw.
class Scripted final : public agents::Agent {
public:
    void act(const agents::AgentView& view,
             math::rng::RngEngine& rng,
             std::vector<agents::Action>& out) override {
        ++acts;
        last_tick = view.tick;
        last_time = view.time;
        last_elapsed = view.elapsed;
        last_cash = view.account.cash;
        last_buying_power = view.account.buying_power;
        last_positions.assign(view.account.positions.begin(), view.account.positions.end());
        last_orders.assign(view.open_orders.begin(), view.open_orders.end());
        last_factors.assign(view.factors.begin(), view.factors.end());
        last_asset_count = view.assets.size();
        draws.push_back(rng());
        out = to_ask;
    }

    void on_fill(const agents::Fill& fill) override { fills.push_back(fill); }

    void on_reject(const agents::Action& action, agents::RejectReason reason) override {
        rejects.emplace_back(action, reason);
    }

    std::vector<agents::Action> to_ask;

    int acts = 0;
    std::int64_t last_tick = 0;
    double last_time = 0.0;
    double last_elapsed = 0.0;
    double last_cash = 0.0;
    double last_buying_power = 0.0;
    std::vector<mm::Quantity> last_positions;
    std::vector<agents::OpenOrder> last_orders;
    std::vector<double> last_factors;
    std::size_t last_asset_count = 0;
    std::vector<std::uint64_t> draws;
    std::vector<agents::Fill> fills;
    std::vector<std::pair<agents::Action, agents::RejectReason>> rejects;
};

} // namespace mms::simulation::tests

#endif // MMS_SIMULATION_TESTS_SCRIPTED_HPP
