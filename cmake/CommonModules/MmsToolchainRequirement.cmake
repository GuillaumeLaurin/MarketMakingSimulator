# ==============================================================================
#  MmsToolchainRequirement.cmake   (CommonModules)
#  Analogue of TinyORM's cmake/CommonModules/TinyToolchainRequirement.cmake
#
#  mms_toolchain_requirement(MSVC <ver> GCC <ver> CLANG <ver> APPLECLANG <ver>)
#  Aborts configuration when the active C++ compiler is older than the minimum
#  known-good version. Unlisted compilers pass with an informational note.
# ==============================================================================

include_guard(GLOBAL)

function(mms_toolchain_requirement)
    set(oneValueArgs MSVC GCC CLANG APPLECLANG)
    cmake_parse_arguments(PARSE_ARGV 0 REQ "" "${oneValueArgs}" "")

    set(_id "${CMAKE_CXX_COMPILER_ID}")
    set(_ver "${CMAKE_CXX_COMPILER_VERSION}")
    set(_min "")

    if(_id STREQUAL "MSVC")
        set(_min "${REQ_MSVC}")
    elseif(_id STREQUAL "GNU")
        set(_min "${REQ_GCC}")
    elseif(_id STREQUAL "Clang")
        set(_min "${REQ_CLANG}")
    elseif(_id STREQUAL "AppleClang")
        set(_min "${REQ_APPLECLANG}")
    endif()

    if(NOT _min)
        message(STATUS "MMS: no minimum version recorded for ${_id} ${_ver} -- proceeding")
        return()
    endif()

    if(_ver VERSION_LESS _min)
        message(FATAL_ERROR
            "MMS requires ${_id} >= ${_min} for complete C++20 support, "
            "but found ${_id} ${_ver}.")
    endif()

    message(STATUS "MMS: toolchain ${_id} ${_ver} satisfies the minimum ${_min}")
endfunction()
