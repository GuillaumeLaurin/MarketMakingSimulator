#ifndef MMS_SIMULATION_RESULTS_OBSERVER_HPP
#define MMS_SIMULATION_RESULTS_OBSERVER_HPP

/// \file
/// ResultsObserver: the SimulationObserver that builds a run's Results
/// (SIM23). It counts trades as they happen and, at the end of every tick,
/// reads the P&L ledger: the tick's CHANGE in total P&L goes to the running
/// statistics and the P&L LEVEL goes to the drawdown tracker (SIM31). Any
/// other observer (a CSV writer, a future GUI) can watch the same run beside
/// it.

#include <vector>

#include <mm/pnl.hpp>
#include <mm/types.hpp>

#include <simulation/results.hpp>
#include <simulation/simulation_export.hpp>
#include <simulation/simulation_observer.hpp>

namespace mms::simulation {

class MMS_SIMULATION_EXPORT ResultsObserver final : public SimulationObserver {
public:
    /// Watches `ledger`, which must outlive the observer (it is World's own).
    /// The level of the ledger right now is the starting point of the
    /// drawdown history, so a run that only ever loses is measured from where
    /// it began, not from its first loss.
    explicit ResultsObserver(const mm::PnlLedger& ledger);

    void on_trade(mm::AssetId asset, const mm::Trade& trade, bool aggressor_is_mm) override;

    void on_tick_end(double time) override;

    /// What has been gathered so far; complete once the run is over.
    [[nodiscard]] const Results& results() const noexcept { return results_; }

private:
    const mm::PnlLedger& ledger_;
    Results results_;
    std::vector<double> previous_total_;
    double previous_portfolio_total_;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_RESULTS_OBSERVER_HPP