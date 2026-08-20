# ==============================================================================
#  MmsModule.cmake   (Modules: build-only)  --  mms_add_module()
#
#  The module factory. Turns  include/<name>/ + src/<name>/  into a versioned,
#  symbol-hidden SHARED library (STATIC when BUILD_SHARED_LIBS=OFF):
#
#      target :  mms_<name>
#      alias  :  MMS::<name>
#      export :  <name>/<name>_export.hpp   (generate_export_header, installed)
#
#  This mirrors how TinyORM builds its TinyOrm / TinyDrivers / TinyMySql
#  libraries: one factory, one export set (MmsTargets), one package config.
# ==============================================================================

include_guard(GLOBAL)

include(GenerateExportHeader)
include(MmsSources)
include(MmsResourceAndManifest)
include(MmsPkgConfig)

# Every module target registers here so MmsDeployment.cmake can iterate them.
set_property(GLOBAL PROPERTY MMS_MODULE_TARGETS "")

# ------------------------------------------------------------------------------
#  mms_add_module(<name>
#      [PUBLIC_DEPENDS  <target>...]   linked & propagated (other MMS:: modules,
#                                      Threads::Threads, imported third-party libs)
#      [PRIVATE_DEPENDS <target>...]   linked, not propagated
#      [SOURCES <file>...]  [HEADERS <file>...]   bypass the default globs
#      [NO_INSTALL])                   build it, keep it out of the package
# ------------------------------------------------------------------------------
function(mms_add_module name)
    set(options        NO_INSTALL)
    set(multiValueArgs PUBLIC_DEPENDS PRIVATE_DEPENDS SOURCES HEADERS)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "${options}" "" "${multiValueArgs}")

    if(DEFINED ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "mms_add_module(${name}) got unexpected arguments: "
            "${ARG_UNPARSED_ARGUMENTS}")
    endif()

    set(_target   "mms_${name}")
    set(_mod_dir  "${PROJECT_SOURCE_DIR}/modules/${name}")
    set(_inc_dir  "${_mod_dir}/include/${name}")
    string(TOUPPER "${name}" _NAME_UC)

    mms_collect_module_sources(${name} _sources _headers
        SOURCES ${ARG_SOURCES} HEADERS ${ARG_HEADERS})

    # -- the library ---------------------------------------------------------------
    add_library(${_target} ${_sources} ${_headers})
    add_library(MMS::${name} ALIAS ${_target})

    set_target_properties(${_target} PROPERTIES
        OUTPUT_NAME                "mms_${name}"
        EXPORT_NAME                "${name}"
        VERSION                    "${PROJECT_VERSION}"
        SOVERSION                  "${PROJECT_VERSION_MAJOR}"
        CXX_VISIBILITY_PRESET      hidden
        VISIBILITY_INLINES_HIDDEN  ON
        WINDOWS_EXPORT_ALL_SYMBOLS OFF)

    # Option B: each module owns its own include root; dependents pick up a
    # module's headers transitively via target_link_libraries(... MMS::<dep>).
    target_include_directories(${_target}
        PUBLIC
            "$<BUILD_INTERFACE:${_mod_dir}/include>"
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/export>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
        PRIVATE
            "${_mod_dir}/src")

    target_link_libraries(${_target}
        PUBLIC
            MMS::version
            ${ARG_PUBLIC_DEPENDS}
        PRIVATE
            # $<BUILD_INTERFACE:...> keeps the warning flags out of the exported
            # link interface: find_package(MMS) consumers don't inherit them.
            "$<BUILD_INTERFACE:mms_common>"
            ${ARG_PRIVATE_DEPENDS})

    # -- export / visibility macro header ------------------------------------
    #   generated header -> `#include <name/name_export.hpp>`   (MMS_<NAME>_EXPORT)
    generate_export_header(${_target}
        BASE_NAME        "MMS_${_NAME_UC}"
        EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/export/${name}/${name}_export.hpp")

    if(NOT BUILD_SHARED_LIBS)
        target_compile_definitions(${_target} PUBLIC "MMS_${_NAME_UC}_STATIC_DEFINE")
    endif()

    # -- Windows file-version resource -------------------------------------------
    mms_resource_and_manifest(${_target} NAME "MMS ${name} module" TYPE Library)

    # -- install --------------------------------------------------------------
    if(MMS_INSTALL AND NOT ARG_NO_INSTALL)
        install(TARGETS ${_target}
            EXPORT MmsTargets
            RUNTIME  DESTINATION "${CMAKE_INSTALL_BINDIR}"
            LIBRARY  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            ARCHIVE  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

        if(EXISTS "${_inc_dir}")
            install(DIRECTORY "${_inc_dir}"
                DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
                FILES_MATCHING PATTERN "*.hpp" PATTERN "*.hxx" PATTERN "*.h")
        endif()

        install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/export/${name}/${name}_export.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${name}")

        if(MMS_BUILD_PKGCONFIG)
            mms_generate_pkgconfig(${_target} ${name}
                PUBLIC_DEPENDS ${ARG_PUBLIC_DEPENDS})
        endif()

        set_property(GLOBAL APPEND PROPERTY MMS_MODULE_TARGETS ${_target})
    endif()

    message(STATUS "  + module MMS::${name}  (${_target})")
endfunction()
