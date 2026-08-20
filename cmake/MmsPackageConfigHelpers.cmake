# ==============================================================================
#  MmsPackageConfigHelpers.cmake
#  Installed next to MMSConfig.cmake and included by it at find_package() time.
#  Analogue of TinyORM's cmake/TinyPackageConfigHelpers.cmake.
# ==============================================================================

include_guard(GLOBAL)

# mms_package_announce(<pkg-name> <linkage>)
# Emits the "Found MMS x.y.z (shared) ..." status line consumers expect.
function(mms_package_announce pkg linkage)
    if(${pkg}_FIND_QUIETLY)
        return()
    endif()
    set(_req "")
    if(${pkg}_FIND_VERSION)
        set(_req " (requested ${${pkg}_FIND_VERSION}${${pkg}_FIND_VERSION_RANGE})")
    endif()
    message(STATUS
        "Found ${pkg} ${${pkg}_VERSION} (${linkage})${_req} at ${CMAKE_CURRENT_LIST_DIR}")
endfunction()
