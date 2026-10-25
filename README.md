# Market Making Simulator

[![CI][ci-badge]][ci]
[![License: MIT][license-badge]][license]
[![C++20][cpp-badge]][cpp]
[![CMake >= 3.25][cmake-badge]][cmake]
[![vcpkg][vcpkg-badge]][vcpkg]

A simulator of a market maker: an agent that keeps a buy price and a sell price in the
market, tries to earn the spread between them, and has to survive the inventory it
collects along the way. Prices move on their own, other participants trade against the
quotes, and messages take time to arrive.

I am building it to understand three things end to end: how an order book matches, how
inventory risk shapes quoting, and what latency costs a strategy. It is also practice at
designing C++ data structures where speed counts. It is not a high-frequency trading
system.

## Where it stands

The engine works and is tested. There is no application yet: `src/app` is empty, so today
you use it as a library from C++.

What a run does now:

- Moves the true prices of a basket of correlated assets, with a random walk or a factor
  model, arithmetic or geometric.
- Runs one order book and matching engine per asset.
- Runs agents: a market maker that quotes with an Avellaneda-Stoikov strategy over the whole
  portfolio, with risk limits, and bots (informed flow, a noise trader, a trend follower, a
  mean reverter, a factor trader). Each has its own cash, speed, delays and account, under
  the same rules.
- Checks every order against the account, keeps track of who owns each resting order, and
  never lets an agent trade with itself.
- Shows each agent the best levels of the books, the latest trades and, if it is meant to
  have it, the true price, as of some time ago if the agent is slow.
- Keeps a P&L ledger per agent and reports its statistics, drawdown, trade counts and
  refusals.
- Replays exactly from a seed.

What it is turning into: a sandbox where a person can take a seat among the bots as a trader
or a market maker, assets can be listed while it runs, and the whole thing can be saved and
resumed. The bots are built. None of the rest is. It is written up in
[`docs/architecture/future`](docs/architecture/future/).

## Architecture

The code is split into modules, with dependencies only pointing down. The pages in
[`docs/architecture`](docs/architecture/README.md) explain what was chosen and why, with
diagrams.

| Page | Covers |
|---|---|
| [Overview](docs/architecture/overview.md) | Modules, the dependency rule, shared libraries |
| [Randomness](docs/architecture/randomness.md) | Counter-based streams, keys by symbol |
| [Price processes](docs/architecture/price-processes.md) | Random walk, factor model, arithmetic and geometric |
| [Order book and matching](docs/architecture/order-book-and-matching.md) | The book, the engine, the latency queue |
| [Market making](docs/architecture/market-making.md) | Quoting, risk, requoting, P&L |
| [Agents](docs/architecture/agents.md) | What an agent sees, the account rules, ownership, the bots, timing |
| [The simulation loop](docs/architecture/simulation-loop.md) | What happens in a tick |
| [Observers and results](docs/architecture/observers-and-results.md) | How a run reports |
| [Logging and diagnostics](docs/architecture/logging-and-diagnostics.md) | Levels, sinks, traced exceptions |
| [Build and CI](docs/architecture/build-and-ci.md) | Presets, the pipeline |
| [Performance](docs/architecture/performance.md) | The time budget, the benchmarks |
| [Known limits](docs/architecture/limitations.md) | What the model gets wrong |
| [Planned](docs/architecture/future/) | The human seat, real time, save and resume, dynamic assets, sweeps |

## Getting started

### Prerequisites

| Tool | Version | Notes |
|---|---|---|
| CMake | >= 3.25 | presets version 6 |
| Ninja | any recent | default generator for every preset |
| C++ compiler | MSVC >= 19.30 (VS 2022), GCC >= 11, or Clang >= 14 | full C++20 |
| vcpkg | any recent | set `VCPKG_ROOT`; dependencies come from `vcpkg.json` |
| Doxygen + Graphviz | optional | only for the `docs` target |
| clang-format, clang-tidy | optional | only for the local checks |

### Configure, build, test

vcpkg installs the dependencies the first time you configure.

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

On Windows with an MSYS2 UCRT64 toolchain, use the `*-mingw` presets instead.

| Preset | Build type | Contents |
|---|---|---|
| `dev` / `dev-mingw` | Debug | modules, tests, benchmarks (built, not run), warnings as errors |
| `release` / `release-mingw` | Release | optimised, LTO, no tests |
| `bench` / `bench-mingw` | Release | benchmarks only, for measuring |
| `asan` | Debug | AddressSanitizer and UBSan |
| `coverage` | Debug | `--coverage` for `gcovr` |

### One module

Every module is the target `mms_<name>` with the alias `MMS::<name>`: `core`, `math`,
`mm`, `agents`, `simulation`, `logging`, `diagnostics`.

```bash
cmake --build --preset dev --target mms_core
```

Options can be turned off at configure time:

```bash
cmake --preset dev -DMMS_BUILD_APP=OFF -DMMS_BUILD_TESTS=OFF -DMMS_BUILD_BENCHMARKS=OFF
```

### Tests and benchmarks

```bash
ctest --preset dev                 # all unit tests (Catch2)
ctest --preset dev -R <regex>      # a subset
```

Tests live in `modules/<name>/tests/` and benchmarks in `modules/<name>/bench/`. Drop a
`*.cpp` in either and it is picked up on the next configure, with no CMake edit.

Benchmarks only mean something in Release:

```bash
cmake --preset bench
cmake --build --preset bench
build/bench/out/bench/simulation_runner_ticks
```

See [performance](docs/architecture/performance.md) for what each one measures and for
the MinGW `PATH` caveat.

### The application

The executable target is `mms_app` (binary `mms`), but `src/app` has no sources, so it
builds a placeholder that does nothing.

### Install and consume as a package

```bash
cmake --install build/dev --prefix /some/prefix
```

```cmake
find_package(MMS 0.1 REQUIRED)
target_link_libraries(your_app PRIVATE MMS::simulation)
```

## Development

The config files are at the root: `.clang-format`, `.clang-tidy`, `CMakePresets.json`,
`vcpkg.json`. The scripts in `ci/` run the same commands as CI.

```bash
bash ci/check-format.sh           # fails on any difference
clang-format -i $(git ls-files '*.hpp' '*.cpp')   # fix in place
bash ci/check-tidy.sh dev         # clang-tidy, tests and benchmarks skipped
bash ci/sanitize.sh asan          # build and test under ASan and UBSan
bash ci/coverage.sh               # writes build/coverage/coverage.xml
```

`clang-tidy` is advisory for now: nothing is promoted to an error (see the top of
`.clang-tidy`). The API reference is built with Doxygen:

```bash
cmake --preset dev -DMMS_BUILD_DOCS=ON
cmake --build --preset dev --target docs
# open build/dev/docs/html/index.html
```

For VS Code, `.vscode/setup_intellisense.ps1` generates the local settings from a preset
(default `dev-mingw`). Only that script and `extensions.json` are tracked.

## Contributing

It is a learning project, so clarity wins over cleverness and small changes are easier to
review. [`CONTRIBUTING.md`](CONTRIBUTING.md) has the details. The short version: branch
off `main`, write commit subjects as `type(scope): summary`, run `ci/check-format.sh` and
`ci/build.sh dev` before opening a PR, and add a Catch2 test for new behaviour.

Other documents: [`CODING_STYLE.md`](CODING_STYLE.md), [`CHANGELOG.md`](CHANGELOG.md),
[`ci/README.md`](ci/README.md).

## License

MIT. See [`LICENSE`](LICENSE).

## References

- Avellaneda, M., & Stoikov, S. (2008). *High-frequency trading in a limit order book*.

<!-- Badge references -->
[ci-badge]: https://github.com/GuillaumeLaurin/MMS/actions/workflows/ci.yml/badge.svg
[ci]: https://github.com/GuillaumeLaurin/MMS/actions/workflows/ci.yml
[license-badge]: https://img.shields.io/badge/license-MIT-blue.svg
[license]: LICENSE
[cpp-badge]: https://img.shields.io/badge/C%2B%2B-20-00599C.svg?logo=cplusplus
[cpp]: https://en.cppreference.com/w/cpp/20
[cmake-badge]: https://img.shields.io/badge/CMake-3.25%2B-064F8C.svg?logo=cmake
[cmake]: https://cmake.org/
[vcpkg-badge]: https://img.shields.io/badge/deps-vcpkg-1e6fbf.svg
[vcpkg]: https://vcpkg.io/
