#ifndef MMS_SIMULATION_TICK_LOOP_HPP
#define MMS_SIMULATION_TICK_LOOP_HPP

/// \file
/// TickLoop: advances a World one tick at a time, in a fixed order: the true prices move
/// and every account is marked, adverse flow is scheduled, each agent whose decision
/// period has come round acts (in the order of the config) and its messages are scheduled
/// after their own latency, then everything due is executed in (due time, scheduling
/// order) order. Because adverse flow is scheduled before the agents act, at equal due
/// times it reaches the exchange first and hits the previous tick's, now stale, quotes:
/// the adverse selection this whole simulation exists to measure.
///
/// Events execute at tick boundaries: one due between two ticks runs at the next one.
///
/// Every order that rests at an exchange is recorded with its owner, so that a trade is
/// settled in the accounts of both parties. An agent never trades with itself: the engines
/// cancel the older of its own orders instead (self-trade prevention).
///
/// What an agent is shown is still partial: no book levels and no recent trades yet, and
/// the true price only as the signal of the market maker and of informed flow.
///
/// The observers still speak of one market maker: the first market maker of the config.
/// Its quotes are the ones notified and returned by live_quotes(), and a trade started by
/// any agent counts as the market maker's.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <agents/agent_view.hpp>
#include <mm/requote.hpp>
#include <mm/types.hpp>

#include <simulation/agent_id.hpp>
#include <simulation/order_registry.hpp>
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
    /// Borrows `world` (it must outlive the loop) and takes what it needs from `config`:
    /// dt, the adverse-flow parameters of each asset and the tick sizes. Validates dt and
    /// the adverse-flow specs up front, so a bad config fails here and not at some tick
    /// deep into the run. Throws std::invalid_argument.
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

    /// The first market maker's quotes as they currently rest in the books: for each side
    /// of each asset, its oldest order that has reached the exchange. Empty slots when the
    /// config has no market maker.
    [[nodiscard]] mm::LiveQuoteSet live_quotes() const;

    /// How many resting orders the loop knows the owner of. It is the number of orders
    /// resting in all the books together, since only agents leave orders there.
    [[nodiscard]] std::size_t recorded_orders() const noexcept { return registry_.size(); }

private:
    class EngineObserver;

    /// The order an agent has put in an exchange and that is being matched right now.
    struct Submission {
        AgentId agent;
        Ticket ticket;
        mm::Side side;
        mm::Quantity filled = 0;
    };

    void schedule_adverse_flow(const math::Vector& prices);
    void run_agents(const math::Vector& prices);
    void execute(const SimulationEvent& event);
    void execute_submit(const SubmitArrival& arrival);
    void execute_cancel(const CancelArrival& arrival);
    void on_trade(mm::AssetId asset, const mm::Trade& trade);
    void on_order_cancelled(mm::AssetId asset, mm::OrderId id);
    [[nodiscard]] std::optional<OrderOwner>
    owner_of(mm::AssetId asset, mm::OrderId id, mm::Side side) const;
    void touch(AgentId agent, mm::AssetId asset);
    void notify_quote_changes();

    World& world_;
    double dt_;
    LatencyConfig latency_;
    std::vector<AssetSpec> assets_;

    double time_ = 0.0;
    std::int64_t ticks_ = 0;
    math::Vector previous_prices_;
    OrderRegistry registry_;
    std::optional<Submission> submission_;
    std::optional<AgentId> observed_maker_;
    std::vector<bool> shown_signal_;                ///< per agent: gets AssetView::signal
    std::vector<agents::AssetView> plain_markets_;  ///< what an agent without a signal sees
    std::vector<agents::AssetView> signal_markets_; ///< the same with the true prices
    std::vector<SimulationObserver*> observers_;
    std::vector<std::unique_ptr<EngineObserver>> engine_observers_;
    std::vector<bool> quotes_changed_;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_TICK_LOOP_HPP
