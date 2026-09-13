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
    # Best-effort commit hash: falls back to "unknown" outside a git checkout
    # (e.g. a source tarball) or if git itself isn't installed - never a
    # configure error. Used by MMS::diagnostics' error dumps (DIAG4).
    set(MMS_GIT_COMMIT_HASH "unknown")
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE _git_hash
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _git_result)
        if(_git_result EQUAL 0 AND _git_hash)
            set(MMS_GIT_COMMIT_HASH "${_git_hash}")
        endif()
    endif()

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
