# ==============================================================================
#  MmsVersion.cmake   (Modules: build-only)
#  Analogue of TinyORM's tiny_read_version()/version.hpp handling -- here the
#  project() version is the single source of truth and the header is generated.
#
#  mms_generate_version_header()
#     -> ${PROJECT_BINARY_DIR}/gen/mms/version.hpp   (from cmake/version.hpp.in)
#     -> interface target  MMS::version  (linked PUBLIC by every module, so
#        `#include <mms/version.hpp>` resolves in the build tree and once installed)
# ==============================================================================

include_guard(GLOBAL)

function(mms_generate_version_header)
    set(_out "${PROJECT_BINARY_DIR}/gen/mms/version.hpp")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/version.hpp.in" "${_out}" @ONLY)

    add_library(mms_version INTERFACE)
    add_library(MMS::version ALIAS mms_version)
    set_target_properties(mms_version PROPERTIES EXPORT_NAME version)

    target_compile_features(mms_version INTERFACE cxx_std_20)
    target_include_directories(mms_version INTERFACE
        "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/gen>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

    if(MMS_INSTALL)
        install(FILES "${_out}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mms")
    endif()
endfunction()
