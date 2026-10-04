/// \file
/// Implementation of ResultsObserver (<simulation/results_observer.hpp>).

#include <cstddef>

#include <simulation/results_observer.hpp>

namespace mms::simulation {

namespace {

void record(AssetResults& target, const mm::PnlBreakdown& breakdown, double& previous_total) {
    const double total = breakdown.total();
    target.pnl_increments.add(total - previous_total);
    target.equity_drawdown.add(total);
    target.final_breakdown = breakdown;
    previous_total = total;
}

} // namespace

ResultsObserver::ResultsObserver(const mm::PnlLedger& ledger)
    : ledger_(ledger), previous_total_(ledger.asset_count(), 0.0),
      previous_portfolio_total_(ledger.portfolio().total()) {
    results_.assets.resize(ledger.asset_count());
    for (std::size_t asset = 0; asset < results_.assets.size(); ++asset) {
        previous_total_[asset] = ledger.breakdown(asset).total();
        results_.assets[asset].equity_drawdown.add(previous_total_[asset]);
        results_.assets[asset].final_breakdown = ledger.breakdown(asset);
    }
    results_.portfolio.equity_drawdown.add(previous_portfolio_total_);
    results_.portfolio.final_breakdown = ledger.portfolio();
}

void ResultsObserver::on_trade(mm::AssetId asset,
                               const mm::Trade& /*trade*/,
                               bool aggressor_is_mm) {
    AssetResults& per_asset = results_.assets.at(asset);
    for (AssetResults* target : {&per_asset, &results_.portfolio}) {
        if (aggressor_is_mm) {
            ++target->mm_trade_count;
        } else {
            ++target->adverse_trade_count;
        }
    }
}

void ResultsObserver::on_tick_end(double /*time*/) {
    for (std::size_t asset = 0; asset < results_.assets.size(); ++asset) {
        record(results_.assets[asset], ledger_.breakdown(asset), previous_total_[asset]);
    }
    record(results_.portfolio, ledger_.portfolio(), previous_portfolio_total_);
}

} // namespace mms::simulation