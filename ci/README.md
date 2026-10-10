# `ci/` continuous-integration scripts

Thin wrappers around CMake presets so the exact commands CI runs are also
runnable locally. GitHub Actions (`.github/workflows/ci.yml`) only orchestrates
(matrix, caching, triggers) and calls into these.

| Script | Purpose |
|---|---|
| `lib.sh` | shared shell helpers (sourced, not executed) |
| `setup-linux.sh` | install toolchain (cmake, ninja, gcc/clang, clang-format/tidy, gcovr) + bootstrap vcpkg |
| `setup-windows.ps1` | ensure ninja + vcpkg (MSVC env comes from the workflow) |
| `build.sh [preset]` | `cmake --preset` → `--build` → `ctest` (preset defaults to `dev`) |
| `check-format.sh` | `clang-format --dry-run --Werror` over `modules/ src/ include/` |
| `check-tidy.sh [preset]` | `clang-tidy` over the module / app sources using the preset's compile DB, in parallel (`MMS_TIDY_JOBS`); tests and benchmarks are skipped unless `MMS_TIDY_INCLUDE_TESTS=1`; `MMS_TIDY_SHARD=<i>/<n>` keeps every n-th file (CI runs 4 shards); `MMS_TIDY_BASE=<git ref>` keeps only what changed since that ref (a changed header re-checks its whole module); `MMS_TIDY_LIST_ONLY=1` just prints the selection |
| `sanitize.sh [preset]` | `build.sh` with ASan/UBSan env (preset defaults to `asan`) |
| `coverage.sh` | build the `coverage` preset, run tests, write `build/coverage/coverage.xml` |

## Local usage

```sh
export VCPKG_ROOT=/path/to/vcpkg      # or run ci/setup-linux.sh
ci/build.sh dev
ci/check-format.sh
ci/sanitize.sh asan
ci/coverage.sh
```

Presets live in `CMakePresets.json`: `dev`, `release`, `bench`, `asan`, `coverage`
(+ `dev-mingw`, `release-mingw` and `bench-mingw` for a local MSYS2/MinGW toolchain).
`dev` builds the benchmarks but only `bench` is meant for running them: Debug timings
mean nothing.

## What the workflow runs

| Job | What it does |
|---|---|
| guard | checks there is any C++ under `modules/` or `src/`, so a bare checkout runs nothing |
| build | `build.sh dev` on Linux GCC, Linux Clang and Windows MSVC |
| lint | `check-format.sh` (on shard 0 only) and `check-tidy.sh`, split over four runners with `MMS_TIDY_SHARD` |
| sanitizers | `sanitize.sh asan` |
| coverage | `coverage.sh`, report uploaded as an artifact |
| docs | the Doxygen `docs` target, uploaded as an artifact |

On a pull request the lint job only analyses what changed (`MMS_TIDY_BASE`). It analyses
everything when `.clang-tidy`, a CMake file, `CMakePresets.json`, `vcpkg.json`,
`check-tidy.sh` or `ci.yml` changes, and on a push to `main`.

Benchmarks are compiled by the build job and never run in CI.

## The vcpkg cache

Every job restores the vcpkg binary cache at the start and saves it at the end, even when
the build fails. The key ends with the run id, so each run can save a new cache, and the
restore falls back to older keys by prefix. A key that only depended on `vcpkg.json` would
stay a hit for ever and go stale when the runner image changes its compiler. The lint job
saves from shard 0 only. The reasoning is in
[build and CI](../docs/architecture/build-and-ci.md).
