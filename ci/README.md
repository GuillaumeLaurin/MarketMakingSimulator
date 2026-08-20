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
| `check-tidy.sh [preset]` | `clang-tidy` over the module / app sources using the preset's compile DB |
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

Presets live in `CMakePresets.json`: `dev`, `release`, `asan`, `coverage`
(+ `dev-mingw` / `release-mingw` for a local MSYS2/MinGW toolchain).
