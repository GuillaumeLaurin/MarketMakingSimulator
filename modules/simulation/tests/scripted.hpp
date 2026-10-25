#ifndef MMS_SIMULATION_TESTS_SCRIPTED_HPP
#define MMS_SIMULATION_TESTS_SCRIPTED_HPP

/// \file
/// Test scaffolding: an agent that asks for whatever the test tells it to and remembers what
/// it was shown and told.

#include <cstddef>
#include <cstdint>
#include <optional>
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
        last_bids.clear();
        last_asks.clear();
        last_trades.clear();
        last_tick_sizes.clear();
        last_signals.clear();
        for (const agents::AssetView& market : view.assets) {
            last_tick_sizes.push_back(market.tick_size);
            last_signals.push_back(market.signal);
            last_bids.emplace_back(market.bids.begin(), market.bids.end());
            last_asks.emplace_back(market.asks.begin(), market.asks.end());
            last_trades.emplace_back(market.recent_trades.begin(), market.recent_trades.end());
        }
        draws.push_back(rng());
        out = to_ask;
        if (once) {
            to_ask.clear();
        }
    }

    void on_fill(const agents::Fill& fill) override { fills.push_back(fill); }

    void on_reject(const agents::Action& action, agents::RejectReason reason) override {
        rejects.emplace_back(action, reason);
    }

    std::vector<agents::Action> to_ask;

    /// Ask for `to_ask` once, at the next act, and then for nothing.
    bool once = false;

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
    std::vector<mm::Price> last_tick_sizes;
    std::vector<std::optional<mm::Price>> last_signals;
    std::vector<std::vector<agents::BookLevel>> last_bids;
    std::vector<std::vector<agents::BookLevel>> last_asks;
    std::vector<std::vector<agents::TradePrint>> last_trades;
    std::vector<std::uint64_t> draws;
    std::vector<agents::Fill> fills;
    std::vector<std::pair<agents::Action, agents::RejectReason>> rejects;
};

} // namespace mms::simulation::tests

#endif // MMS_SIMULATION_TESTS_SCRIPTED_HPP
