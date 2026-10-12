#ifndef MMS_SIMULATION_RESULTS_OBSERVER_HPP
#define MMS_SIMULATION_RESULTS_OBSERVER_HPP

/// \file
/// ResultsObserver: the SimulationObserver that builds a run's Results
/// (SIM23), one AgentResults per agent. It counts trades and refusals as they happen and, at
/// the end of every tick, reads each agent's P&L ledger: the tick's CHANGE in total P&L goes to
/// the running statistics and the P&L LEVEL goes to the drawdown tracker (SIM31). Any other
/// observer (a CSV writer, a future GUI) can watch the same run beside it.

#include <string>
#include <vector>

#include <mm/pnl.hpp>
#include <mm/types.hpp>

#include <simulation/results.hpp>
#include <simulation/simulation_export.hpp>
#include <simulation/simulation_observer.hpp>
#include <simulation/world.hpp>

namespace mms::simulation {

/// An agent to report on: its name and the ledger of its account.
struct ObservedAgent {
    std::string name;
    const mm::PnlLedger* ledger;
};

/// Every agent of the world, in the order of the config. The ledgers are the world's own.
[[nodiscard]] MMS_SIMULATION_EXPORT std::vector<ObservedAgent> observed_agents(const World& world);

class MMS_SIMULATION_EXPORT ResultsObserver final : public SimulationObserver {
public:
    /// Watches these agents, whose ledgers must outlive the observer. The level of each ledger
    /// right now is the starting point of its drawdown history, so a run that only ever loses
    /// is measured from where it began, not from its first loss. Throws std::invalid_argument
    /// for a null ledger.
    explicit ResultsObserver(std::vector<ObservedAgent> agents);

    /// Counts the trade for the agent that started it and for the one that was hit, on that
    /// asset and in total. Throws std::out_of_range for an unknown asset or agent, and counts
    /// nothing then.
    void on_trade(mm::AssetId asset, const mm::Trade& trade, const TradeParties& parties) override;

    /// Counts the refusal under its reason. Throws std::out_of_range for an unknown agent.
    void
    on_reject(AgentId agent, const agents::Action& action, agents::RejectReason reason) override;

    void on_tick_end(double time) override;

    /// What has been gathered so far; complete once the run is over.
    [[nodiscard]] const Results& results() const noexcept { return results_; }

private:
    struct Watched {
        const mm::PnlLedger* ledger;
        std::vector<double> previous_total; ///< per asset
        double previous_portfolio_total;
    };

    std::vector<Watched> watched_;
    Results results_;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_RESULTS_OBSERVER_HPP
