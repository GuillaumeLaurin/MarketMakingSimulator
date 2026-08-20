# Unit tests for MMS::simulation. Every *.cpp here becomes its own CTest
# executable, linked against the module (core/math/mms come transitively).
# No files yet -> no-op.
file(GLOB _tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(_test IN LISTS _tests)
    get_filename_component(_name "${_test}" NAME_WE)
    mms_add_test(simulation_${_name} SOURCES "${_test}" LINK MMS::simulation)
endforeach()
