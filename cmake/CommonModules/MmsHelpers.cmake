# ==============================================================================
#  MmsHelpers.cmake  --  generic helpers (CommonModules: installed & reusable)
#  Analogue of TinyORM's cmake/CommonModules/TinyHelpers.cmake
#
#  Everything here is safe to `include()` from a downstream project after
#  find_package(MMS) -- it defines functions only, it has no side effects.
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
#  mms_target_name(<module-name> <out-var>)   ->  mms_<module-name>
# ------------------------------------------------------------------------------
function(mms_target_name name out_var)
    set(${out_var} "mms_${name}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
#  mms_discover_modules(<out-var> [ROOT <modules-dir>] [REQUIRE_INCLUDE])
#
#  Return the sorted list of module names = first-level sub-directories of the
#  modules/ root (defaults to ${PROJECT_SOURCE_DIR}/modules). Self-contained
#  layout (option B): each module lives in  modules/<name>/{include,src,tests}/.
#
#  REQUIRE_INCLUDE keeps only directories that actually expose public headers
#  (modules/<name>/include/<name>/), i.e. library modules. The executable
#  (modules/simulation/) has no such folder and is filtered out.
# ------------------------------------------------------------------------------
function(mms_discover_modules out_var)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "REQUIRE_INCLUDE" "ROOT" "")
    if(NOT ARG_ROOT)
        set(ARG_ROOT "${PROJECT_SOURCE_DIR}/modules")
    endif()

    file(GLOB _entries RELATIVE "${ARG_ROOT}" "${ARG_ROOT}/*")

    set(_modules "")
    foreach(_e IN LISTS _entries)
        if(NOT IS_DIRECTORY "${ARG_ROOT}/${_e}")
            continue()
        endif()
        if(ARG_REQUIRE_INCLUDE AND NOT IS_DIRECTORY "${ARG_ROOT}/${_e}/include/${_e}")
            continue()
        endif()
        list(APPEND _modules "${_e}")
    endforeach()

    list(SORT _modules)
    set(${out_var} "${_modules}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
#  mms_set_output_directories([<bin-dir> <lib-dir>])
#
#  Route every runtime / library / archive artifact of every configuration into
#  a single pair of directories so the executable finds its module libraries in
#  the build tree without an install step (cf. TinyORM's BUILD_TREE_DEPLOY).
#  Call at directory scope (it needs PARENT_SCOPE to reach the caller).
# ------------------------------------------------------------------------------
function(mms_set_output_directories)
    set(_bin "${ARGV0}")
    set(_lib "${ARGV1}")
    if(NOT _bin)
        set(_bin "${PROJECT_BINARY_DIR}/out/bin")
    endif()
    if(NOT _lib)
        set(_lib "${PROJECT_BINARY_DIR}/out/lib")
    endif()

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_bin}" PARENT_SCOPE)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_bin}" PARENT_SCOPE)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_lib}" PARENT_SCOPE)

    get_property(_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_multi)
        foreach(_cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
            string(TOUPPER "${_cfg}" _CFG)
            set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${_bin}" PARENT_SCOPE)
            set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${_bin}" PARENT_SCOPE)
            set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_CFG} "${_lib}" PARENT_SCOPE)
        endforeach()
    endif()
endfunction()
