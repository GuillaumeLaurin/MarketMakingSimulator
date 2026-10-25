# Unit tests for MMS::application. Every *.cpp in this directory becomes its own
# CTest executable, linked against the module.
file(GLOB _tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(_test IN LISTS _tests)
    get_filename_component(_name "${_test}" NAME_WE)
    mms_add_test(application_${_name} SOURCES "${_test}" LINK MMS::application)
endforeach()
