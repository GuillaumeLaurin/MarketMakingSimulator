/// \file
/// Implementation of ResultsObserver (<simulation/results_observer.hpp>).

#include <cstddef>
#include <stdexcept>
#include <utility>

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

std::vector<ObservedAgent> observed_agents(const World& world) {
    std::vector<ObservedAgent> observed;
    observed.reserve(world.agent_count());
    for (AgentId id = 0; id < world.agent_count(); ++id) {
        const AgentRuntime& runtime = world.agent(id);
        observed.push_back(
            {.name = runtime.account().profile().name, .ledger = &runtime.account().ledger()});
    }
    return observed;
}

ResultsObserver::ResultsObserver(std::vector<ObservedAgent> agents) {
    for (ObservedAgent& agent : agents) {
        if (agent.ledger == nullptr) {
            throw std::invalid_argument("ResultsObserver: an observed agent has no ledger");
        }
        const mm::PnlLedger& ledger = *agent.ledger;

        Watched watched{.ledger = &ledger,
                        .previous_total = std::vector<double>(ledger.asset_count(), 0.0),
                        .previous_portfolio_total = ledger.portfolio().total()};
        AgentResults results{.name = std::move(agent.name), .assets = {}, .portfolio = {}};
        results.assets.resize(ledger.asset_count());
        for (std::size_t asset = 0; asset < results.assets.size(); ++asset) {
            watched.previous_total[asset] = ledger.breakdown(asset).total();
            results.assets[asset].equity_drawdown.add(watched.previous_total[asset]);
            results.assets[asset].final_breakdown = ledger.breakdown(asset);
        }
        results.portfolio.equity_drawdown.add(watched.previous_portfolio_total);
        results.portfolio.final_breakdown = ledger.portfolio();

        watched_.push_back(std::move(watched));
        results_.agents.push_back(std::move(results));
    }
}

void ResultsObserver::on_trade(mm::AssetId asset,
                               const mm::Trade& /*trade*/,
                               const TradeParties& parties) {
    // Looked up before anything is counted, so a bad id leaves the counts as they were.
    AgentResults& aggressive = results_.agents.at(parties.aggressive());
    AgentResults& passive = results_.agents.at(parties.passive());
    AssetResults& aggressive_asset = aggressive.assets.at(asset);
    AssetResults& passive_asset = passive.assets.at(asset);

    ++aggressive_asset.aggressive_trades;
    ++aggressive.portfolio.aggressive_trades;
    ++passive_asset.passive_trades;
    ++passive.portfolio.passive_trades;
}

void ResultsObserver::on_reject(AgentId agent,
                                const agents::Action& /*action*/,
                                agents::RejectReason reason) {
    ++results_.agents.at(agent).rejects.at(static_cast<std::size_t>(reason));
}

void ResultsObserver::on_tick_end(double /*time*/) {
    for (std::size_t agent = 0; agent < watched_.size(); ++agent) {
        Watched& watched = watched_[agent];
        AgentResults& results = results_.agents[agent];
        for (std::size_t asset = 0; asset < results.assets.size(); ++asset) {
            record(results.assets[asset],
                   watched.ledger->breakdown(asset),
                   watched.previous_total[asset]);
        }
        record(results.portfolio, watched.ledger->portfolio(), watched.previous_portfolio_total);
    }
}

} // namespace mms::simulation
