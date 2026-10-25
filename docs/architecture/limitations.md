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
- **The bots are simple.** A noise trader, a trend follower, a mean reverter, a factor trader
  and informed flow. None of them learns, none of them holds a view of fair value, and none
  of them reacts to the others except through the book. Real participants do all three.
  (Gap. The [agents](agents.md) are the place to add more.)
- **One quote per side per asset for the market maker.** The strategy keeps one resting order
  on each side, not a ladder. (Choice for now.)
- **Only the maker fee exists.** No taker fee, no borrow cost, no financing. (Gap.)
- **No liquidation.** An account can go below zero buying power, which blocks every order
  that adds risk, but nothing forces the position closed. Margin is a collateral rate and
  nothing more. (Gap.)
- **Trades have no side effects on prices.** The true price does not react to what the
  agents do, however large they get. A big buyer moves the book, not the process. (Choice.)
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
- **No human seat, no assets added during a run.** Both are planned. See the `future/` pages.
- **`logging` and `diagnostics` are unused by the engine.** Adopting `Traced` exceptions
  is open work. See [logging and diagnostics](logging-and-diagnostics.md).
- **Only one market-making strategy.** Avellaneda-Stoikov. The interface allows more.
- **Agents cannot be saved yet in full.** An agent can write its own state as bytes. The
  market maker does not include its volatility estimators, and nothing writes the rest of the
  world. See [save and resume](future/save-and-resume.md).
- **Agent kinds are a closed list.** Adding one means changing the project. There is no way
  yet to plug one in from outside. See [agents](agents.md).
- **Informed flow has one set of parameters for every asset.** Not one per asset.

## Engineering

- **The order book is the reference version.** `std::map` and `std::deque`. The benchmark
  gives no reason to replace it yet. (See [performance](performance.md).)
- **Performance is measured, not tuned.** And measured only on a modest config, on one
  machine.
- **Benchmarks are not in CI.** On purpose, so there is no automatic regression check.
- **clang-tidy ignores tests and benchmarks.** On purpose, for the time.
