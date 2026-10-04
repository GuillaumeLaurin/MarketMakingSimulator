#ifndef MMS_SIMULATION_TICK_LOOP_HPP
#define MMS_SIMULATION_TICK_LOOP_HPP

/// \file
/// TickLoop: advances a World one tick at a time, in the fixed order of
/// SIM2: the true prices move and are marked, the strategy sees them, adverse
/// flow and the market maker's own requests are scheduled (each after its
/// own latency), then everything due is executed in (due time, scheduling
/// order) order. Because adverse flow is scheduled before the quote
/// requests, at equal due times it reaches the exchange first and hits the
/// previous tick's, now stale, quotes: the adverse selection this whole
/// simulation exists to measure.
///
/// Events execute at tick boundaries : one due between two ticks runs
/// at the next one, at that tick's mid. At most one request per (asset,
/// side) is outstanding at a time : a side with a request in flight is
/// not re-planned until it has executed.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <mm/quote.hpp>
#include <mm/requote.hpp>
#include <mm/types.hpp>

#include <simulation/simulation_config.hpp>
#include <simulation/simulation_export.hpp>
#include <simulation/simulation_observer.hpp>
#include <simulation/world.hpp>

#include "math/types.hpp"
#include "simulation/asset_spec.hpp"
#include "simulation/latency_config.hpp"

namespace mms::simulation {

class MMS_SIMULATION_EXPORT TickLoop {
public:
    /// Borrows `world` (it must outlive the loop) and takes what it needs
    /// from `config`: dt, the requote threshold, the latency delays, the
    /// strategy's tick sizes and each asset's adverse-flow parameters.
    /// Primes the strategy once with the initial mids. Validates the delay
    /// specs and the threshold up front, so a bad config fails here and not
    /// at some tick deep into the run. Throws std::invalid_argument.
    TickLoop(World& world, const SimulationConfig& config);

    TickLoop(const TickLoop&) = delete;
    TickLoop& operator=(const TickLoop&) = delete;
    ~TickLoop();

    /// Observers are not owned and must outlive the loop. Several may watch
    /// the same run.
    void add_observer(SimulationObserver& observer);

    /// Runs one tick.
    void step();

    /// Simulation time of the last completed tick; 0 before the first.
    [[nodiscard]] double time() const noexcept { return time_; }

    [[nodiscard]] std::int64_t tick_count() const noexcept { return ticks_; }

    /// The market maker's quotes as they currently rest in the books.
    [[nodiscard]] const mm::LiveQuoteSet& live_quotes() const noexcept { return live_; }

private:
    class EngineObserver;

    void schedule_adverse_flow(const math::Vector& prices);
    void schedule_requests(const math::Vector& prices);
    void execute(const SimulationEvent& event);
    void on_trade(mm::AssetId asset, const mm::Trade& trade);
    void notify_quote_changes();

    World& world_;
    double dt_;
    mm::Ticks requote_threshold_;
    std::vector<mm::Price> tick_sizes_;
    LatencyConfig latency_;
    std::vector<AssetSpec> assets_;

    double time_ = 0.0;
    std::int64_t ticks_ = 0;
    math::Vector previous_prices_;
    mm::LiveQuoteSet live_;
    std::vector<std::vector<int>> in_flight_; // per asset, per side: requests not yet executed
    std::vector<SimulationObserver*> observers_;
    std::vector<std::unique_ptr<EngineObserver>> engine_observers_;
    // Keyed by (asset, id): every engine numbers its orders from 1, so an id alone is ambiguous.
    std::map<std::pair<mm::AssetId, mm::OrderId>, mm::Side> cancel_sides_;
    std::optional<mm::Side> submitting_side_;
    mm::Quantity submit_filled_ = 0;
    std::vector<bool> quotes_changed_;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_TICK_LOOP_HPP