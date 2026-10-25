# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `MMS::math`: numeric helpers and a counter-based random generator (Philox) with one
  stream per subsystem and asset; samplers for the uniform, exponential, Poisson and normal
  distributions; running statistics, EWMA and drawdown tracking; a validated, factorised
  covariance matrix; the price processes (`CorrelatedRandomWalk`, `FactorModel`); and the
  Avellaneda-Stoikov formulas.
- `MMS::core`: a generic order book and matching engine (limit and market orders, GTC, IOC
  and FOK) and a latency queue that keeps equal due times in insertion order.
- `MMS::mm`: the market-making logic. The quoting strategy (Avellaneda-Stoikov over a
  portfolio), risk limits, the requote plan, tick-grid arithmetic, positions and a P&L
  ledger that explains profit two ways.
- `MMS::simulation`: the engine. A world built from a config, a tick loop with a fixed
  order of events, latency on every message, an observer interface, a results observer, and
  a runner (`SimulationRunner`, `run_simulation`).
- `MMS::agents`: what trades. The `Agent` interface, `AgentView` (the best levels of the
  books, the trades since the agent last looked, the true price for the agents meant to have
  it, the factor moves for the factor traders, its own account and orders) and `Action`; the
  market maker as an agent; and the bots: informed flow, a noise trader, a trend follower, a
  mean reverter and a factor trader. One function, `make_agent`, builds any of them from plain
  data.
- Accounts and the rules of the exchange, in `MMS::simulation`: cash, reserves for pending
  orders, collateral for shorts, position limits, and six reasons for refusing an action, the
  same for every agent. Each agent has its own speed (`decision_period`), delay for its orders
  (`order_delay`) and delay for what it sees (`info_delay`).
- `MMS::core`: self-trade prevention in the matching engine (the older of the submitter's own
  orders is cancelled instead of trading) and `OrderBook::levels`, the best levels of a side
  with their totals.
- `MMS::math`: the price processes expose the moves of their common factors
  (`factor_increments`), and the random streams `Agent` and `AgentLatency`.
- Per-agent results: a P&L ledger, statistics, drawdown, the trades each agent started and
  the ones that hit it, and its refusals by reason.
- `MMS::logging` and `MMS::diagnostics`: leveled logging with pluggable sinks, and
  exceptions that capture a stack trace where they are thrown. Neither is used by the
  engine yet.
- Geometric dynamics for both price processes, selected with `dynamics` in
  `RandomWalkSpec` and `FactorModelSpec`. `mu` is then the expected return rate and the
  volatilities are relative. The Ito correction is computed in `math`.
- Benchmarks (nanobench) for a price step, the matching engine with and without
  observers, and whole-run ticks per second. The `bench` and `bench-mingw` presets build
  them in Release.
- `docs/architecture`: how the engine is put together and why, with diagrams, and what is
  planned (agents and a human seat, real time, save and resume, dynamic assets, sweeps).
- The build and tooling: a modular CMake build with one shared library per module, a vcpkg
  manifest with a pinned baseline, presets (`dev`, `release`, `asan`, `coverage`, plus
  `*-mingw`), the CI workflow and the `ci/` scripts that mirror it, the coding style and
  contribution guides, issue and PR templates, the Doxygen `docs` target, and editor config.

### Changed

- **Breaking:** the market maker is an agent. `SimulationConfig` loses `strategy`,
  `latency` and `requote_threshold_ticks` and gains `agents`, `account_rules` and
  `book_depth`. The strategy and the requote threshold now live in `MarketMakerSpec`, and
  the delay of its orders in the agent's timing. `AssetSpec` gains `tick_size`.
- **Breaking:** adverse flow is gone as a separate mechanism. `AssetSpec` loses its
  `adverse_flow_*` fields, and `generate_adverse_flow`, `AdverseOrder` and `LatencyConfig`
  are removed. The same behaviour is an agent, `InformedFlowSpec`, with one set of
  parameters for every asset. Listed before the market maker, it hits the previous tick's
  quotes as before.
- **Breaking:** the observer. `on_trade` takes the two parties and the side that started the
  trade, `on_reject` is new, `on_quote` is removed and `TickLoop::live_quotes()` becomes
  `quotes_of(agent)`. `Results` holds one entry per agent, and `ResultsObserver` takes the
  agents to watch. `World` loses `pnl_ledger()` and `strategy()` for `agent(i)`.
- A market maker that crosses its own stale quote no longer trades with itself. The older
  order is cancelled and the new one rests.
- **Breaking:** the default price dynamics is now geometric. A config that does not set
  `dynamics` gets relative `mu` and `price_volatility` where it used to get price units.
  Set `dynamics = Dynamics::Arithmetic` to keep the old behaviour. The `math` process
  constructors take the dynamics as a required last argument.
- **Breaking:** `AssetSpec` has a `symbol`, which must be non-empty and unique. An asset's
  random streams are now derived from its symbol and not from its position, so adding,
  removing or reordering assets no longer changes the others' draws. This changes the
  random numbers of any existing seed.
- CI: clang-tidy went from about 22 minutes to a few. It skips tests and benchmarks, is
  split over four runners, and on a pull request only looks at what changed. The vcpkg
  cache is saved on every run, so it no longer goes stale when the runner image changes.
  The Windows test link race (several tests copying the same DLLs) is fixed.

[Unreleased]: https://github.com/GuillaumeLaurin/MMS/commits/main
