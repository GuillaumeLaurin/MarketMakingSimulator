# Market Making Simulator {#mainpage}

A simulator of a market maker: an agent that keeps a buy price and a sell price in the
market, earns the spread between them, and has to survive the inventory it collects.
Prices move on their own, other participants trade against the quotes, and messages take
time to arrive.

This is the API reference. For why the code is shaped the way it is, read the
[architecture pages](https://github.com/GuillaumeLaurin/MMS/blob/main/docs/architecture/README.md).
They have diagrams, and those render on GitHub but not here.

## Library layout

| Target | Responsibility |
|---|---|
| `MMS::math` | random numbers, distributions, price processes, covariance, statistics, the Avellaneda-Stoikov formulas |
| `MMS::core` | order book, matching engine, latency queue, order and trade types |
| `MMS::mm` | market-making logic: quoting, inventory and risk, positions and P&L |
| `MMS::simulation` | the engine: world, tick loop, adverse flow, latency, observers, results, runner. No I/O, no CLI, no UI |
| `MMS::logging` | leveled logging with pluggable sinks |
| `MMS::diagnostics` | exceptions that capture a stack trace where they are thrown |
| application (`src/`) | the command line frontend. Not written yet |

Dependencies only point down:
`app` then `MMS::simulation` then `MMS::mm` then `{ MMS::core, MMS::math }`.
`MMS::diagnostics` depends on `MMS::logging`, and the engine uses neither of them yet.

## Navigating this reference

Use the **Namespaces** and **Classes** tabs above. Each module lives in its own namespace
(`mms::core`, `mms::math`, `mms::mm`, `mms::simulation`, `mms::logging`,
`mms::diagnostics`) and its public API is the set of types tagged with that module's
export macro.

## Where to start reading

* `simulation/simulation_runner.hpp`: build a run from a `SimulationConfig` and step it.
* `simulation/simulation_config.hpp`: everything a run needs.
* `simulation/world.hpp` and `simulation/tick_loop.hpp`: what a run holds and the order of
  events inside a tick.
* `mm/quoting_strategy.hpp`: the interface a strategy implements.

## Conventions

See `CODING_STYLE.md` at the repository root for naming, header layout, symbol visibility
and the module-boundary rules.

## References

* Avellaneda, M., & Stoikov, S. (2008). *High-frequency trading in a limit order book*.
