# simulation

The engine. It builds a world from a config, advances it tick by tick, and reports what
happens to observers. It does no I/O: no files, no command line, no screen.

Depends on `core`, `math` and `mm`.

Start with `simulation/simulation.hpp` (the umbrella). To run something, build a
`SimulationConfig` (`simulation/simulation_config.hpp`) and hand it to
`SimulationRunner` (`simulation/simulation_runner.hpp`). The state of a run is
`simulation/world.hpp` and the order of events in a tick is `simulation/tick_loop.hpp`.

Why it is built this way: [the simulation loop](../../docs/architecture/simulation-loop.md)
and [observers and results](../../docs/architecture/observers-and-results.md).
