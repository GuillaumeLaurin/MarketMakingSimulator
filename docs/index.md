# Market Making Simulator {#mainpage}

A simplified but realistic simulation of a **market maker**: an agent that
provides liquidity by continuously quoting bid and ask prices, manages inventory
risk, and is subject to latency between decision and execution.

## Library layout

| Target | Responsibility |
|---|---|
| `MMS::math` | stochastic price processes, RNG, numeric helpers |
| `MMS::core` | order book, matching engine, latency model, order/trade types |
| `MMS::mms` | market-making domain logic: quoting, inventory and risk, P&L |
| `MMS::simulation` | the engine: tick loop, configuration, adverse order flow, observer interface. Context-free (no I/O, no CLI, no UI) |
| application (`src/`) | the headless CLI frontend: argument parsing, config and result formats, console output |

Dependency direction is strictly one way:
`app` then `MMS::simulation` then `MMS::mms` then `{ MMS::core, MMS::math }`.

## Navigating this reference

Use the **Namespaces** and **Classes** tabs above. Each module lives in its own
namespace (`mms::core`, `mms::math`, `mms::mms`, `mms::simulation`) and its
public API is the set of types tagged with that module's export macro.

The full API tree fills in as the modules gain source files; on a bare checkout
this reference contains only the project overview and the coding style.

## Conventions

See `CODING_STYLE.md` at the repository root for naming, header layout, symbol
visibility and the module-boundary rules.

## References

* Avellaneda, M., & Stoikov, S. (2008). *High-frequency trading in a limit order book*.
