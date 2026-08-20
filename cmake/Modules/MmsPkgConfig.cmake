# ==============================================================================
#  MmsPkgConfig.cmake   (Modules: build-only)
#  Slim analogue of TinyORM's cmake/Modules/GeneratePkgConfig -- emits one
#  pkg-config file per module so non-CMake build systems can consume MMS with
#  `pkg-config --cflags --libs mms-core`.
#
#  mms_generate_pkgconfig(<target> <name> [PUBLIC_DEPENDS <target>...])
# ==============================================================================

include_guard(GLOBAL)

function(mms_generate_pkgconfig target name)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "" "" "PUBLIC_DEPENDS")

    # Translate MMS:: public deps into `Requires:` entries (pkg-config name
    # convention: mms-<module>); anything else is ignored here.
    set(_requires "")
    foreach(_dep IN LISTS ARG_PUBLIC_DEPENDS)
        if(_dep MATCHES "^MMS::(.+)$")
            list(APPEND _requires "mms-${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(JOIN _requires " " MMS_PC_REQUIRES)

    set(MMS_PC_NAME        "mms-${name}")
    set(MMS_PC_DESCRIPTION "MMS ${name} module")
    set(MMS_PC_VERSION     "${PROJECT_VERSION}")
    set(MMS_PC_LIBNAME     "mms_${name}")

    set(_out "${PROJECT_BINARY_DIR}/pkgconfig/mms-${name}.pc")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/pkgconfig/mms.pc.in" "${_out}" @ONLY)

    install(FILES "${_out}"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endfunction()
