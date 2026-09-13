# ==============================================================================
#  MmsDependencies.cmake   (Modules: build-only)
#  Single place where every third-party dependency is resolved. In manifest mode
#  vcpkg installs them from vcpkg.json before CMake runs; the vcpkg toolchain
#  file is supplied by CMakePresets.json ("dev" / "release" presets).
#
#  Modules then just reference the imported targets:
#     Eigen3::Eigen  fmt::fmt  tomlplusplus::tomlplusplus  nlohmann_json::nlohmann_json
#     CLI11::CLI11  Catch2::Catch2WithMain  trompeloeil::trompeloeil  nanobench::nanobench
# ==============================================================================

include_guard(GLOBAL)

# --- always required ---------------------------------------------------------
find_package(fmt CONFIG REQUIRED)              # string formatting / console + CSV
find_package(Eigen3 CONFIG REQUIRED)           # linear algebra for MMS::math
find_package(tomlplusplus CONFIG REQUIRED)     # simulation config files (TOML)
find_package(nlohmann_json CONFIG REQUIRED)    # results / scenario export (JSON)
find_package(CLI11 CONFIG REQUIRED)            # simulation executable argument parsing
find_package(cpptrace CONFIG REQUIRED)         # stacktrace capture for MMS::diagnostics

# --- test-only (vcpkg feature "tests") -------------------------------------
if(MMS_BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)       # unit-test framework
    find_package(trompeloeil CONFIG REQUIRED)  # mocking
endif()

# --- benchmark-only (vcpkg feature "benchmarks") --------------------------
if(MMS_BUILD_BENCHMARKS)
    find_package(nanobench CONFIG REQUIRED)    # micro-benchmarks (map vs unordered_map vs deque)
endif()

message(STATUS "MMS: third-party dependencies resolved via ${CMAKE_TOOLCHAIN_FILE}")
