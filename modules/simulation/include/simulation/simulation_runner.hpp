#ifndef MMS_SIMULATION_SIMULATION_RUNNER_HPP
#define MMS_SIMULATION_SIMULATION_RUNNER_HPP

/// \file
/// SimulationRunner: one complete run, from a SimulationConfig to a Results.
/// It owns the World, the TickLoop that advances it and the ResultsObserver
/// that gathers the statistics, and can be driven tick by tick (a GUI, a
/// debugger) or all at once (run()). run_simulation() is the one-call form
/// the sweep schedules inside each of its tasks.
///
/// A failure inside a run (for example a true price that reaches zero or
/// below, which the strategy rejects) propagates unchanged out of step() or
/// run(). The runner never returns partial results that look complete.

#include <cstdint>

#include <simulation/results.hpp>
#include <simulation/results_observer.hpp>
#include <simulation/simulation_config.hpp>
#include <simulation/simulation_export.hpp>
#include <simulation/simulation_observer.hpp>
#include <simulation/tick_loop.hpp>
#include <simulation/world.hpp>

namespace mms::simulation {

class MMS_SIMULATION_EXPORT SimulationRunner {
public:
    /// Builds the World, the TickLoop and the ResultsObserver from `config`.
    /// Throws std::invalid_argument if `config.tick_count` is negative, or
    /// for anything World or TickLoop reject. A `tick_count` of 0 is valid:
    /// the run is finished from the start and results() are the starting
    /// ones.
    explicit SimulationRunner(const SimulationConfig& config);

    /// The loop and the observer hold references into the World, so a runner
    /// can be neither copied nor moved. The sweep builds each one in place.
    SimulationRunner(const SimulationRunner&) = delete;
    SimulationRunner& operator=(const SimulationRunner&) = delete;
    SimulationRunner(SimulationRunner&&) noexcept = delete;
    SimulationRunner& operator=(SimulationRunner&&) noexcept = delete;
    ~SimulationRunner() = default;

    /// Extra observers (a CSV writer, a GUI) watch the same run beside the
    /// built-in ResultsObserver. Not owned; they must outlive the runner.
    void add_observer(SimulationObserver& observer);

    /// Runs one tick. Throws std::logic_error if the run is already finished.
    void step();

    /// Runs every remaining tick. Does nothing once the run is finished.
    /// Returns results(), so `runner.run()` reads as "the answer".
    const Results& run();

    [[nodiscard]] bool finished() const noexcept { return loop_.tick_count() >= tick_count_; }

    /// Ticks the run is configured for.
    [[nodiscard]] std::int64_t tick_count() const noexcept { return tick_count_; }

    /// What has been gathered so far; complete once finished().
    [[nodiscard]] const Results& results() const noexcept { return observer_.results(); }

    /// Read-only view of the run's state (prices, books, ledger) between ticks.
    [[nodiscard]] const World& world() const noexcept { return world_; }

    /// Ticks run so far, time of the last tick, and the live quotes.
    [[nodiscard]] const TickLoop& loop() const noexcept { return loop_; }

private:
    std::int64_t tick_count_;
    // Declaration order is destruction order reversed: the observer and the
    // loop, which refer to the World, go first.
    World world_;
    TickLoop loop_;
    ResultsObserver observer_;
};

/// Builds a runner for `config`, runs it to the end and returns its results
/// by value. Everything the call touches is local, so concurrent calls with
/// different configs do not interact.
[[nodiscard]] MMS_SIMULATION_EXPORT Results run_simulation(const SimulationConfig& config);

} // namespace mms::simulation

#endif // MMS_SIMULATION_SIMULATION_RUNNER_HPP