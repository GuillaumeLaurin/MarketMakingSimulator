# Micro-benchmarks for MMS::core (nanobench). Every *.cpp here becomes its own
# benchmark executable in build/out/bench/. No files yet -> no-op.
# This is where the README's std::map / std::unordered_map / std::deque
# comparison for the order book belongs.
file(GLOB _benches CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(_bench IN LISTS _benches)
    get_filename_component(_name "${_bench}" NAME_WE)
    mms_add_benchmark(core_${_name} SOURCES "${_bench}" LINK MMS::core)
endforeach()
