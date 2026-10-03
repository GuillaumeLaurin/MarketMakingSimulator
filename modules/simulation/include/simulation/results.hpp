#ifndef MMS_SIMULATION_RESULTS_HPP
#define MMS_SIMULATION_RESULTS_HPP

/// \file
/// The data a run's results are made of. Pure data: the concrete
/// SimulationObserver that feeds these accumulators as the run progresses
/// is results_observer.hpp, built once World/tick_loop exist to drive it.

#include <cstdint>
#include <vector>

#include <math/stats/drawdown.hpp>
#include <math/stats/running_stats.hpp>
#include <mm/pnl.hpp>

namespace mms::simulation {

/// One asset's results, or the portfolio's: the same shape either way,
/// mirroring mm::PnlLedger::breakdown(asset)/portfolio() already returning
/// the same PnlBreakdown type for both.
struct AssetResults {
    /// Per-tick CHANGE in total P&L: summary statistics (mean, stddev, min,
    /// max) of the P&L time series itself, fed one increment at a time.
    math::stats::RunningStats pnl_increments;

    /// Peaks and dips of the running total P&L LEVEL, fed at every mark.
    math::stats::DrawdownTracker equity_drawdown;

    /// Trades where the market maker's own order was the aggressor.
    std::int64_t mm_trade_count = 0;

    /// Trades where adverse flow was the aggressor.
    std::int64_t adverse_trade_count = 0;

    /// The P&L breakdown at the end of the run.
    mm::PnlBreakdown final_breakdown;
};

/// The full result of one run.
struct Results {
    /// One entry per asset, in AssetId order.
    std::vector<AssetResults> assets;

    /// The whole portfolio, same shape as a single asset's.
    AssetResults portfolio;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_RESULTS_HPP