#ifndef MMS_SIMULATION_SIMULATION_OBSERVER_HPP
#define MMS_SIMULATION_SIMULATION_OBSERVER_HPP

/// \file
/// SimulationObserver: the event interface a run notifies as it progresses.
/// Same pattern as core::MatchingEngineObserver: sparse no-op virtuals, so an
/// observer that only cares about trades overrides nothing else, and
/// non-owning, so several observers (one building Results, one writing CSV,
/// a future GUI) can watch the same run side by side. Whoever drives the
/// run (World/Simulation) owns the list of observers; this header only
/// defines the interface.

#include <agents/action.hpp>
#include <agents/reject_reason.hpp>
#include <mm/types.hpp>

#include <simulation/agent_id.hpp>

namespace mms::simulation {

/// Who traded, and who started it.
struct TradeParties {
    AgentId buyer;
    AgentId seller;
    /// The side of the order that started the trade: Buy means the buyer is the one who took
    /// what was resting, Sell means the seller did.
    mm::Side aggressor;

    /// The agent whose order started the trade.
    [[nodiscard]] AgentId aggressive() const noexcept {
        return aggressor == mm::Side::Buy ? buyer : seller;
    }

    /// The agent whose resting order was hit.
    [[nodiscard]] AgentId passive() const noexcept {
        return aggressor == mm::Side::Buy ? seller : buyer;
    }
};

class SimulationObserver {
public:
    virtual ~SimulationObserver() = default;

    /// A new tick has started, at this simulation time.
    virtual void on_tick_start(double /*time*/) {}

    /// The true price of `asset` is now `mid` (after PriceProcess::advance,
    /// before anything else happens this tick).
    virtual void on_mark(mm::AssetId /*asset*/, mm::Price /*mid*/) {}

    /// A trade happened on `asset`, between these two agents.
    virtual void
    on_trade(mm::AssetId /*asset*/, const mm::Trade& /*trade*/, const TradeParties& /*parties*/) {}

    /// The gateway refused something `agent` asked for.
    virtual void on_reject(AgentId /*agent*/,
                           const agents::Action& /*action*/,
                           agents::RejectReason /*reason*/) {}

    /// The tick that started at on_tick_start is complete: every mark and fill
    /// of this tick has happened, so the P&L ledgers are now
    /// final for this tick. This is the moment to read them.
    virtual void on_tick_end(double /*time*/) {}

protected:
    SimulationObserver() = default;
    SimulationObserver(const SimulationObserver&) = default;
    SimulationObserver& operator=(const SimulationObserver&) = default;
    SimulationObserver(SimulationObserver&&) noexcept = default;
    SimulationObserver& operator=(SimulationObserver&&) noexcept = default;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_SIMULATION_OBSERVER_HPP
