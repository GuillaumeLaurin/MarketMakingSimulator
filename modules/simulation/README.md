# simulation

The engine. It builds a world from a config, holds an account for every agent and applies
the rules of the exchange, advances the world tick by tick, and reports what happens to
observers. It does no I/O: no files, no command line, no screen.

Depends on `core`, `math`, `mm` and `agents`.

Start with `simulation/simulation.hpp` (the umbrella). To run something, build a
`SimulationConfig` (`simulation/simulation_config.hpp`) and hand it to
`SimulationRunner` (`simulation/simulation_runner.hpp`). The state of a run is
`simulation/world.hpp`, one agent as the loop holds it is `simulation/agent_runtime.hpp`,
and the order of events in a tick is `simulation/tick_loop.hpp`.

Why it is built this way: [the simulation loop](../../docs/architecture/simulation-loop.md),
[agents](../../docs/architecture/agents.md) and
[observers and results](../../docs/architecture/observers-and-results.md).
