# Micro-benchmarks for MMS::agents (nanobench). Every *.cpp here becomes its own
# benchmark executable in build/out/bench/. No files yet -> no-op.
file(GLOB _benches CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(_bench IN LISTS _benches)
    get_filename_component(_name "${_bench}" NAME_WE)
    mms_add_benchmark(agents_${_name} SOURCES "${_bench}" LINK MMS::agents)
endforeach()
