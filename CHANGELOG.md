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
  order of events, adverse order flow, latency on every message, an observer interface,
  a results observer, and a runner (`SimulationRunner`, `run_simulation`).
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
