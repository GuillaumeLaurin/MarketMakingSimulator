# ==============================================================================
#  Market Making Simulator  --  top-level build
# ------------------------------------------------------------------------------
#  Modular CMake decomposition, modelled on silverqx/TinyORM:
#
#    cmake/
#      CommonModules/          reusable helpers (on CMAKE_MODULE_PATH, installed)
#        MmsCommon.cmake            mms_common()  -> MMS::common flags interface lib
#        MmsHelpers.cmake           discovery, output dirs, misc utilities
#        MmsToolchainRequirement.cmake   minimum compiler-version gate
#        MmsMsvcParallel.cmake      /MP parallel compilation switch
#        MmsResourceAndManifest.cmake    per-binary Windows VERSIONINFO resource
#      Modules/                 build-only machinery (not installed)
#        MmsInitDefaultVariables.cmake   default CMake vars (call once, early)
#        MmsOptions.cmake           user-facing options + FeatureSummary
#        MmsDependencies.cmake      find_package() for every vcpkg dependency
#        MmsVersion.cmake           generate <mms/version.hpp> -> MMS::version
#        MmsSources.cmake           collect a module's sources / headers
#        MmsModule.cmake            mms_add_module()  -> one dynamic lib MMS::<name>
#        MmsPkgConfig.cmake         emit .pc files for non-CMake consumers
#        MmsDeployment.cmake        install rules + package config (build & install)
#        MmsTestCommon.cmake        mms_add_test()       (Catch2 + trompeloeil)
#        MmsBenchCommon.cmake       mms_add_benchmark()  (nanobench)
#      MmsConfig.cmake.in / MmsBuildTreeConfig.cmake.in / MmsPackageConfigHelpers.cmake
#      pkgconfig/mms.pc.in / version.hpp.in
#
#  Self-contained module layout (option B):
#
#    modules/<name>/
#      include/<name>/   public headers   ->  #include <name/foo.hpp>
#      src/              implementation + private headers
#      tests/            unit tests   (tests/CMakeLists.txt, glob-driven)
#      bench/            benchmarks   (bench/CMakeLists.txt, glob-driven)
#      CMakeLists.txt    declares the module + its dependencies
#
#    modules/<name>/  with include/<name>/  ->  dynamic library  MMS::<name>
#    modules/simulation/                    ->  MMS::simulation (the engine lib;
#                                               context-free: no CLI, no I/O)
#
#  Application layer (the "usage context") lives at the repo root:
#    include/           public headers of the application
#    src/               ->  executable  mms  (headless CLI frontend)
#                           parses args, picks formats, drives MMS::simulation
#    (a GUI would be a sibling app, e.g. apps/gui/, reusing MMS::simulation)
#
#  Dependencies are resolved by vcpkg in manifest mode (vcpkg.json); configure
#  through a preset so the vcpkg toolchain file is picked up:
#      cmake --preset dev       &&  cmake --build --preset dev
# ==============================================================================

cmake_minimum_required(VERSION 3.25)

list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CommonModules"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

project(MMS
    DESCRIPTION  "Market Making Simulator"
    HOMEPAGE_URL "https://github.com/GuillaumeLaurin/MMS"
    LANGUAGES    CXX
    VERSION      0.1.0
)

# --- toolbox -----------------------------------------------------------------
include(MmsHelpers)                 # CommonModules
include(MmsCommon)                  # CommonModules
include(MmsToolchainRequirement)    # CommonModules
include(MmsMsvcParallel)            # CommonModules
include(MmsOptions)                 # Modules  (options first: Init & Deps read them)
include(MmsInitDefaultVariables)    # Modules
include(MmsDependencies)            # Modules  (find_package for every vcpkg dep)
include(MmsVersion)                 # Modules
include(MmsModule)                  # Modules

mms_init_default_variables()

mms_toolchain_requirement(
    MSVC       19.30      # Visual Studio 2022 17.0
    GCC        11
    CLANG      14
    APPLECLANG 14
)

mms_msvc_parallel("Enable MSVC /MP parallel compilation (ON, OFF, or an integer)")

find_package(Threads REQUIRED)

# MMS::common  -- shared warning/flags interface, linked PRIVATE by every module.
mms_common(mms_common NAMESPACE MMS NAME common)

# MMS::version -- generated <mms/version.hpp>, linked PUBLIC by every module.
mms_generate_version_header()

# --- library modules -------------------------------------------------------------
# Every modules/<name>/ exposing include/<name>/ headers is a library module.
mms_discover_modules(MMS_MODULE_NAMES REQUIRE_INCLUDE)
message(STATUS "MMS library modules: ${MMS_MODULE_NAMES}")

foreach(_mod IN LISTS MMS_MODULE_NAMES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/${_mod}/CMakeLists.txt")
        add_subdirectory("modules/${_mod}")
    else()
        mms_add_module(${_mod})
    endif()
endforeach()

# --- application (headless CLI frontend) ------------------------------------------
# The only place that knows a usage context: CLI parsing, config formats, console
# / file output. Turns arguments into a SimulationConfig, drives MMS::simulation,
# attaches concrete observers and prints a summary. A GUI would be a sibling
# application reusing MMS::simulation, never depending on this target.
#
#   include/   application public headers
#   src/*.cpp  application sources + main()
if(MMS_BUILD_APP)
    include(MmsResourceAndManifest)

    file(GLOB_RECURSE _mms_app_sources CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/src/*.cpp"
        "${PROJECT_SOURCE_DIR}/src/*.cxx"
        "${PROJECT_SOURCE_DIR}/src/*.cc")

    # Placeholder entry point so the target builds before real code exists.
    if(NOT _mms_app_sources)
        set(_mms_app_sources "${PROJECT_BINARY_DIR}/gen/mms_app_main_placeholder.cpp")
        if(NOT EXISTS "${_mms_app_sources}")
            file(WRITE "${_mms_app_sources}"
                "#include <mms/version.hpp>\n"
                "#include <iostream>\n\n"
                "int main()\n{\n"
                "    std::cout << \"Market Making Simulator \" << mms::version_string << '\\n';\n"
                "    return 0;\n"
                "}\n")
        endif()
    endif()

    add_executable(mms_app ${_mms_app_sources})
    add_executable(MMS::app ALIAS mms_app)
    set_target_properties(mms_app PROPERTIES OUTPUT_NAME mms)

    target_include_directories(mms_app PRIVATE
        "${PROJECT_SOURCE_DIR}/include"     # application public headers
        "${PROJECT_SOURCE_DIR}/src")        # private headers next to the sources

    target_link_libraries(mms_app
        PRIVATE
            "$<BUILD_INTERFACE:mms_common>"
            MMS::simulation                 # engine; brings core/math/mms transitively
            CLI11::CLI11                    # command-line argument parsing
            fmt::fmt                        # console output
            tomlplusplus::tomlplusplus      # simulation config files (TOML)
            nlohmann_json::nlohmann_json)    # results / scenario export (JSON)

    mms_resource_and_manifest(mms_app NAME "Market Making Simulator" TYPE Executable)

    if(MMS_INSTALL)
        install(TARGETS mms_app RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
endif()

# --- tests / benchmarks (co-located under each module) -----------------------
mms_discover_modules(MMS_ALL_MODULE_DIRS)   # library modules + simulation

if(MMS_BUILD_TESTS)
    enable_testing()
    include(MmsTestCommon)
    foreach(_mod IN LISTS MMS_ALL_MODULE_DIRS)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/${_mod}/tests/CMakeLists.txt")
            add_subdirectory("modules/${_mod}/tests")
        endif()
    endforeach()
endif()

if(MMS_BUILD_BENCHMARKS)
    include(MmsBenchCommon)
    foreach(_mod IN LISTS MMS_ALL_MODULE_DIRS)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/${_mod}/bench/CMakeLists.txt")
            add_subdirectory("modules/${_mod}/bench")
        endif()
    endforeach()
endif()

# --- documentation ----------------------------------------------------------
if(MMS_BUILD_DOCS)
    include(MmsDocs)   # adds the 'docs' target (Doxygen); uses MMS_MODULE_NAMES
endif()

# --- install / find_package(MMS) ----------------------------------------------
if(MMS_INSTALL)
    include(MmsDeployment)
endif()

# --- summary -----------------------------------------------------------------
include(FeatureSummary)
feature_summary(WHAT ENABLED_FEATURES DISABLED_FEATURES
    DESCRIPTION "MMS build configuration:")
