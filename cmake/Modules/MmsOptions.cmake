# ==============================================================================
#  MmsOptions.cmake   (Modules: build-only)
#  Analogue of TinyORM's cmake/Modules/TinyOptions.cmake + TinyFeatureOptions.
# ==============================================================================

include_guard(GLOBAL)

include(CMakeDependentOption)
include(FeatureSummary)

option(BUILD_SHARED_LIBS     "Build each MMS module as a shared library"       ON)
option(MMS_BUILD_TESTS       "Build the MMS test suite (Catch2 + trompeloeil)" OFF)
option(MMS_BUILD_BENCHMARKS  "Build the MMS micro-benchmarks (nanobench)"      OFF)
option(MMS_BUILD_APP         "Build the mms application (headless CLI frontend)" ON)
option(MMS_BUILD_DOCS        "Add the 'docs' target (Doxygen API documentation)" OFF)
option(MMS_INSTALL           "Generate install() + find_package(MMS) rules"    ${PROJECT_IS_TOP_LEVEL})
option(MMS_WERROR            "Treat compiler warnings as errors"               OFF)
option(MMS_ENABLE_LTO        "Enable IPO/LTO for optimized configurations"     OFF)
option(MMS_COVERAGE         "Instrument the build for coverage (--coverage, GCC/Clang)" OFF)
set(MMS_SANITIZE "" CACHE STRING
    "Comma-separated -fsanitize list (e.g. address,undefined); empty to disable")
option(MMS_BUILD_PKGCONFIG   "Also install pkg-config (.pc) files per module"  ON)
cmake_dependent_option(MMS_VERSION_RESOURCE
    "Embed a VERSIONINFO resource into each Windows .dll / .exe" ON
    "WIN32" OFF)

add_feature_info(BUILD_SHARED_LIBS    BUILD_SHARED_LIBS    "modules are dynamic libraries (OFF = static archives)")
add_feature_info(MMS_BUILD_TESTS      MMS_BUILD_TESTS      "compile unit tests (modules/<name>/tests/)")
add_feature_info(MMS_BUILD_BENCHMARKS MMS_BUILD_BENCHMARKS "compile micro-benchmarks (modules/<name>/bench/)")
add_feature_info(MMS_BUILD_APP        MMS_BUILD_APP        "build the mms CLI application (src/)")
add_feature_info(MMS_BUILD_DOCS       MMS_BUILD_DOCS       "Doxygen 'docs' target")
add_feature_info(MMS_INSTALL          MMS_INSTALL          "emit install & package-config rules")
add_feature_info(MMS_WERROR           MMS_WERROR          "warnings are errors")
add_feature_info(MMS_ENABLE_LTO       MMS_ENABLE_LTO      "link-time optimization")
add_feature_info(MMS_COVERAGE         MMS_COVERAGE        "coverage instrumentation (--coverage)")
add_feature_info("MMS_SANITIZE=${MMS_SANITIZE}" MMS_SANITIZE "sanitizers: ${MMS_SANITIZE}")
add_feature_info(MMS_BUILD_PKGCONFIG  MMS_BUILD_PKGCONFIG "install .pc files for non-CMake consumers")
add_feature_info(MMS_VERSION_RESOURCE MMS_VERSION_RESOURCE "Windows file-version metadata in binaries")

if(MMS_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _mms_ipo_ok OUTPUT _mms_ipo_msg)
    if(_mms_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE        ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    else()
        message(WARNING "MMS_ENABLE_LTO requested but unavailable: ${_mms_ipo_msg}")
    endif()
endif()
