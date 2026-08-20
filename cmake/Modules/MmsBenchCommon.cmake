# ==============================================================================
#  MmsBenchCommon.cmake   (Modules: build-only)
#  mms_add_benchmark(): declare a micro-benchmark executable (nanobench).
#  Mirrors MmsTestCommon.cmake. Benchmarks are NOT registered with CTest.
#
#  mms_add_benchmark(<name> SOURCES <file>... [LINK <target>...])
# ==============================================================================

include_guard(GLOBAL)

function(mms_add_benchmark name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LINK")

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "mms_add_benchmark(${name}): SOURCES is required")
    endif()

    set(_target "mms_bench_${name}")
    add_executable(${_target} ${ARG_SOURCES})
    set_target_properties(${_target} PROPERTIES OUTPUT_NAME "${name}")

    target_link_libraries(${_target} PRIVATE
        "$<BUILD_INTERFACE:mms_common>"
        nanobench::nanobench
        ${ARG_LINK})

    # Keep benchmark binaries out of the way of the normal runtime output dir.
    set_target_properties(${_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out/bench")
endfunction()
