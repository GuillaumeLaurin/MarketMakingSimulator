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

#include <mm/quote.hpp>
#include <mm/types.hpp>

namespace mms::simulation {

class SimulationObserver {
public:
    virtual ~SimulationObserver() = default;

    /// A new tick has started, at this simulation time.
    virtual void on_tick_start(double /*time*/) {}

    /// The true price of `asset` is now `mid` (after PriceProcess::advance,
    /// before anything else happens this tick).
    virtual void on_mark(mm::AssetId /*asset*/, mm::Price /*mid*/) {}

    /// The market maker's live quotes on `asset` changed to this (an empty
    /// side means that side is not quoted).
    virtual void on_quote(mm::AssetId /*asset*/, const mm::AssetQuotes& /*quotes*/) {}

    /// A trade happened on `asset`. `aggressor_is_mm` is true when the order of the
    /// (first) market maker started it, false when another agent.s order did.
    virtual void
    on_trade(mm::AssetId /*asset*/, const mm::Trade& /*trade*/, bool /*aggressor_is_mm*/) {}

    /// The tick that started at on_tick_start is complete: every mark, fill
    /// and quote change of this tick has happened, so the P&L ledger is now
    /// final for this tick. This is the moment to read it.
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