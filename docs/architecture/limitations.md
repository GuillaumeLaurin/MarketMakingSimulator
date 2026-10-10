# Known limits

What the model gets wrong, leaves out, or has not been built. Some of it is a choice
and some is work that is not done yet. Each item says which.

## The model

- **Prices are a stochastic process, not a market.** The true price follows a random walk
  or a factor model. There is no volatility clustering, no jumps, no news. A real asset
  has all three. (Choice, for now.)
- **Arithmetic dynamics can still go negative.** Geometric is the default and cannot.
  Arithmetic remains for comparison and for cases where a price in price units is wanted.
  (Choice.)
- **The dense random walk couples assets.** Adding an asset that sorts before another one
  changes the other's price path, because the Cholesky factor changes. The factor model
  does not have the problem. (Known cost of the dense walk.)
- **Factors are independent.** The factor model does not support correlated factors.
  (Choice: nothing needed them.)
- **Counterparties are thin.** The only thing that trades against the market maker is
  adverse flow: market orders that follow the last price move with a configurable
  probability. There are no other limit orders in the book than the market maker's own.
  (Gap, and the main reason to build [agents](future/agents-and-human-seat.md).)
- **One quote per side per asset.** The strategy keeps one resting order on each side, not
  a ladder. (Choice for now.)
- **Only the maker fee exists.** No taker fee, no borrow cost, no financing. (Gap.)
- **No liquidation and no margin.** A position can grow until a risk limit stops the
  strategy from quoting that side, but nothing forces it closed. (Gap.)
- **Timing is only as fine as `dt`.** An event due between two ticks runs at the next
  tick, at that tick's mid. (Cost of a tick-based loop.)
- **Order types are plain.** Limit and market, with GTC, IOC and FOK. No iceberg, no stop.
  Matching is price then time. (Choice.)

## Reproducibility

- **Exact on one machine and one binary. Not across platforms.** The generator and the
  samplers are ours, so only floating-point arithmetic can differ. Nothing yet forces
  `-ffp-contract=off` or turns Eigen's vectorisation off. (Gap. See [randomness](randomness.md).)

## What is not built

- **No application.** `src/app` is empty: no command line, no config files, no result
  files, no GUI.
- **No serialisation.** A run cannot be saved or resumed. See
  [save and resume](future/save-and-resume.md).
- **No agents, no human seat, no assets added during a run.** All of it is planned. See
  the `future/` pages.
- **`logging` and `diagnostics` are unused by the engine.** Adopting `Traced` exceptions
  is open work. See [logging and diagnostics](logging-and-diagnostics.md).
- **Only one strategy.** Avellaneda-Stoikov. The interface allows more.

## Engineering

- **The order book is the reference version.** `std::map` and `std::deque`. The benchmark
  gives no reason to replace it yet. (See [performance](performance.md).)
- **Performance is measured, not tuned.** And measured only on a modest config, on one
  machine.
- **Benchmarks are not in CI.** On purpose, so there is no automatic regression check.
- **clang-tidy ignores tests and benchmarks.** On purpose, for the time.
