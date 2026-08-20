# Market Making Simulator

[![CI][ci-badge]][ci]
[![License: MIT][license-badge]][license]
[![C++20][cpp-badge]][cpp]
[![CMake >= 3.25][cmake-badge]][cmake]
[![vcpkg][vcpkg-badge]][vcpkg]

A simplified but realistic simulation of a **market maker**: an agent that
provides liquidity by continuously quoting bid and ask prices, manages inventory
risk, and is exposed to latency between decision and execution.

## Context and motivation

This project aims to simulate, in a simplified but realistic environment, the behavior of a **market maker**. A market participant who provides liquidity by continuously quoting buy (bid) and sell (ask) prices on an asset.

The goal is not to reproduce a production-grade high-frequency trading system, but to understand and implement, end to end:

- the internal mechanics of an order book,
- the basic principles of inventory risk management,
- the concrete impact of **latency** on the profitability of a quoting strategy.

This project also serves as a training ground for designing efficient data structures in C++, in a context where performance (execution time, algorithmic complexity) genuinely matters.

---

## Learning objectives

- Understand how an order book works and how order matching is performed.
- Model a market price using a stochastic process (random walk).
- Implement quoting logic with inventory risk management (inspired by academic models such as Avellaneda-Stoikov).
- Work with and compare different C++ data structures (`std::map`, `std::unordered_map`, `std::deque`, etc.) based on their algorithmic complexity.
- Measure and analyze a simulated P&L, as well as the effect of latency on strategy performance.

---

## Planned features

- Reference price generator following a random walk (with configurable volatility)
- Simplified order book (add, cancel, execute limit orders)
- Matching engine (FIFO within a given price level)
- Market maker agent with bid/ask quoting logic
- Inventory risk management (quote skew based on position)
- Simulated adverse order flow (market noise)
- Configurable latency between decision and execution
- P&L calculation (realized and mark-to-market)
- Export of results for analysis (CSV / charts)

---

## Evaluation metrics

- Final P&L and its evolution over time
- Average / maximum inventory held
- Number of fills and quote fill rate
- Impact of latency on P&L (comparison across several delay scenarios)
- Sensitivity to parameters (price volatility, spread width, skew aggressiveness)

---

## Known model limitations

- The reference price follows a simple random walk, which does not reproduce real market microstructure (no informed order flow, no volatility clustering, etc.).
- The order book is intentionally simplified (no multi-asset support, no complex order types such as iceberg or stop orders).
- Latency is modeled in a simplified way (fixed delay or basic distribution), without reproducing real network intricacies.

---

## Getting started

### Prerequisites

| Tool | Version | Notes |
|---|---|---|
| CMake | >= 3.25 | presets version 6 |
| Ninja | any recent | default generator for every preset |
| C++ compiler | MSVC >= 19.30 (VS 2022), GCC >= 11, or Clang >= 14 | full C++20 support |
| vcpkg | any recent | set `VCPKG_ROOT`; dependencies come from `vcpkg.json` in manifest mode |
| Doxygen + Graphviz | optional | only for the `docs` target |
| clang-format, clang-tidy | optional | only for the local checks |

### Configure, build, test

Dependencies are installed automatically by vcpkg the first time you configure.

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

On Windows with an MSYS2 / MinGW-w64 UCRT64 toolchain, use the `*-mingw`
presets instead (`dev-mingw`, `release-mingw`).

| Preset | Build type | Contents |
|---|---|---|
| `dev` / `dev-mingw` | Debug | modules, app, tests, benchmarks, warnings as errors |
| `release` / `release-mingw` | Release | optimized, LTO, no tests |
| `asan` | Debug | AddressSanitizer + UndefinedBehaviorSanitizer |
| `coverage` | Debug | `--coverage` instrumentation for `gcovr` |

### Build the application only

The executable target is `mms_app` (binary name `mms`).

```bash
cmake --preset dev
cmake --build --preset dev --target mms_app
./build/dev/out/bin/mms          # mms.exe on Windows
```

### Build a single library module

Every module is the target `mms_<name>` with the alias `MMS::<name>`
(`core`, `math`, `mms`, `simulation`).

```bash
cmake --preset dev
cmake --build --preset dev --target mms_core       # just the order book / matching library
```

To skip the parts you do not need, turn options off at configure time:

```bash
cmake --preset dev -DMMS_BUILD_APP=OFF -DMMS_BUILD_TESTS=OFF -DMMS_BUILD_BENCHMARKS=OFF
cmake --build --preset dev --target mms_math
```

### Tests and benchmarks

```bash
ctest --preset dev                 # all unit tests (Catch2)
ctest --preset dev -R <regex>      # a subset
```

Unit tests live in `modules/<name>/tests/`, benchmarks in `modules/<name>/bench/`
(nanobench). Both directories are picked up automatically; dropping a `*.cpp`
there is enough, no CMake edit required.

### Install and consume as a package

```bash
cmake --install build/dev --prefix /some/prefix
```

```cmake
find_package(MMS 0.1 REQUIRED)
target_link_libraries(your_app PRIVATE MMS::simulation)
```

---

## Development

Configuration files live at the repository root: `.clang-format`, `.clang-tidy`,
`CMakePresets.json`, `vcpkg.json`. The wrapper scripts in `ci/` run the exact
same commands as continuous integration.

### Formatting (clang-format)

```bash
bash ci/check-format.sh                       # report only, fails on any diff (used by CI)
clang-format -i $(git ls-files '*.hpp' '*.cpp' '*.hxx' '*.cxx')   # apply in place
```

### Static analysis (clang-tidy)

`clang-tidy` needs a configured build for its compile database.

```bash
bash ci/check-tidy.sh dev
# or directly:
cmake --preset dev
clang-tidy -p build/dev $(git ls-files 'modules/**/*.cpp' 'src/**/*.cpp')
```

It is advisory for now (no check is promoted to an error); see the note at the
top of `.clang-tidy`.

### Sanitizers and coverage

```bash
bash ci/sanitize.sh asan          # build + test under ASan/UBSan
bash ci/coverage.sh               # writes build/coverage/coverage.xml
```

### Documentation (Doxygen)

```bash
cmake --preset dev -DMMS_BUILD_DOCS=ON
cmake --build --preset dev --target docs
# open build/dev/docs/html/index.html
```

### VS Code

```powershell
.\.vscode\setup_intellisense.ps1            # default preset: dev-mingw
.\.vscode\setup_intellisense.ps1 -Preset dev
```

Generates the local (git-ignored) `c_cpp_properties.json`, `settings.json`,
`tasks.json` and `launch.json` from the chosen preset. Only
`.vscode/extensions.json` and `.vscode/setup_intellisense.ps1` are tracked.

---

## Contributing

Contributions are welcome. This is a learning project, so clarity is valued over
cleverness, and small focused changes are preferred.

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the full guide. In short:

- Fork, add an `upstream` remote, branch off `main` (`feat/`, `fix/`, `docs/`, ...).
- Commit subjects follow Conventional Commits: `type(scope): imperative summary`.
- Before opening a PR: `bash ci/check-format.sh` and `bash ci/build.sh dev` must
  pass, new behavior comes with a Catch2 test, and CI must be green.
- Issue and pull-request templates prompt for everything reviewers need.

---

## Documentation

- [`CODING_STYLE.md`](CODING_STYLE.md): naming, headers, API design, module
  boundaries, error handling.
- [`CONTRIBUTING.md`](CONTRIBUTING.md): setup, issues, commits, pull requests.
- [`CHANGELOG.md`](CHANGELOG.md): notable changes per release.
- `docs/`: Doxygen sources (`index.md`, `Doxyfile.in`). Build the `docs` target
  for the API reference.
- [`ci/README.md`](ci/README.md): what each CI script does.

---

## License

MIT License. See [`LICENSE`](LICENSE).

---

## References

- Avellaneda, M., & Stoikov, S. (2008). *High-frequency trading in a limit order book*.
- C++ standard documentation (cppreference.com) for the data structures used.

---

## Progress

| Module | Status |
|---|---|
| Reference price random walk | To do |
| Order book data structures | To do |
| Order book (add/cancel/matching) | To do |
| Basic quoting logic | To do |
| Inventory risk management | To do |
| Latency simulation | To do |
| P&L calculation and visualization | To do |

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
