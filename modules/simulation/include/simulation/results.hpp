#ifndef MMS_SIMULATION_RESULTS_HPP
#define MMS_SIMULATION_RESULTS_HPP

/// \file
/// The data a run's results are made of: one AgentResults per agent, in the order of the config.
/// Pure data: the SimulationObserver that feeds these accumulators as the run progresses is
/// results_observer.hpp.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <agents/reject_reason.hpp>
#include <mm/pnl.hpp>

#include <math/stats/drawdown.hpp>
#include <math/stats/running_stats.hpp>

namespace mms::simulation {

/// One agent's results on one asset, or on all of them together: the same shape either way,
/// mirroring mm::PnlLedger::breakdown(asset)/portfolio() already returning the same PnlBreakdown
/// type for both.
struct AssetResults {
    /// Per-tick CHANGE in total P&L: summary statistics (mean, stddev, min,
    /// max) of the P&L time series itself, fed one increment at a time.
    math::stats::RunningStats pnl_increments;

    /// Peaks and dips of the running total P&L LEVEL, fed at every mark.
    math::stats::DrawdownTracker equity_drawdown;

    /// Trades started by an order of this agent.
    std::int64_t aggressive_trades = 0;

    /// Trades in which an order of this agent was resting and was hit.
    std::int64_t passive_trades = 0;

    /// The P&L breakdown at the end of the run.
    mm::PnlBreakdown final_breakdown;
};

struct AgentResults {
    /// The agent's name, as in its profile.
    std::string name;

    /// One entry per asset, in AssetId order.
    std::vector<AssetResults> assets;

    /// All the assets together, same shape as a single asset's.
    AssetResults portfolio;

    /// How many of its actions the gateway refused, by reason (index by RejectReason).
    std::array<std::int64_t, agents::reject_reason_count> rejects{};

    /// All the refusals, whatever the reason.
    [[nodiscard]] std::int64_t total_rejects() const noexcept {
        std::int64_t total = 0;
        for (const std::int64_t count : rejects) {
            total += count;
        }
        return total;
    }
};

/// The full result of one run.
struct Results {
    /// One entry per agent, in the order of the config.
    std::vector<AgentResults> agents;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_RESULTS_HPP
