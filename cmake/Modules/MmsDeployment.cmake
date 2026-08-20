# ==============================================================================
#  MmsDeployment.cmake   (Modules: build-only)
#  Analogue of TinyORM's cmake/Modules/TinyDeployment.cmake + the top-level
#  TinyPackageConfigHelpers wiring. Produces:
#
#    * install-tree package :  <prefix>/lib/cmake/MMS/{MMSConfig,MMSConfigVersion,
#                              MmsTargets}.cmake  + the reusable CommonModules
#    * build-tree package   :  <build>/MMSConfig.cmake  (find_package(MMS) against
#                              the build dir, no install needed)
# ==============================================================================

include_guard(GLOBAL)

include(CMakePackageConfigHelpers)

set(MMS_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/MMS"
    CACHE STRING "Install location of MMS's CMake package files")

get_property(MMS_MODULES GLOBAL PROPERTY MMS_MODULE_TARGETS)

# ---------------------------------------------------------------------------
#  Export set  (mms_version is a usage requirement of every module)
# ---------------------------------------------------------------------------
install(TARGETS mms_version
    EXPORT MmsTargets
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(EXPORT MmsTargets
    NAMESPACE MMS::
    FILE MmsTargets.cmake
    DESTINATION "${MMS_INSTALL_CMAKEDIR}")

export(EXPORT MmsTargets
    NAMESPACE MMS::
    FILE "${PROJECT_BINARY_DIR}/MmsTargets.cmake")

# ---------------------------------------------------------------------------
#  Reusable CommonModules shipped with the package (MmsCommon, MmsHelpers, ...)
#  so downstream code can `include(MmsCommon)` after find_package(MMS).
# ---------------------------------------------------------------------------
file(GLOB _mms_common_modules "${PROJECT_SOURCE_DIR}/cmake/CommonModules/*.cmake")
install(FILES
    ${_mms_common_modules}
    "${PROJECT_SOURCE_DIR}/cmake/MmsPackageConfigHelpers.cmake"
    DESTINATION "${MMS_INSTALL_CMAKEDIR}")

# ---------------------------------------------------------------------------
#  Package config + version files
# ---------------------------------------------------------------------------
set(MMS_LINKAGE "static")
if(BUILD_SHARED_LIBS)
    set(MMS_LINKAGE "shared")
endif()

# -- install tree --
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/MmsConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/install/MMSConfig.cmake"
    INSTALL_DESTINATION "${MMS_INSTALL_CMAKEDIR}"
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_BINDIR)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/MMSConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${PROJECT_BINARY_DIR}/install/MMSConfig.cmake"
    "${PROJECT_BINARY_DIR}/MMSConfigVersion.cmake"
    DESTINATION "${MMS_INSTALL_CMAKEDIR}")

# -- build tree --
set(MMS_BUILD_TREE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/CommonModules")
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/MmsBuildTreeConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/MMSConfig.cmake"
    @ONLY)
file(COPY "${PROJECT_BINARY_DIR}/MMSConfigVersion.cmake"
    DESTINATION "${PROJECT_BINARY_DIR}")

# Optional: register the build tree so a bare find_package(MMS) discovers it.
export(PACKAGE MMS)

# ---------------------------------------------------------------------------
#  PDBs for shared MSVC builds
# ---------------------------------------------------------------------------
if(MSVC AND BUILD_SHARED_LIBS)
    foreach(_t IN LISTS MMS_MODULES)
        install(FILES "$<TARGET_PDB_FILE:${_t}>"
            DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL)
    endforeach()
endif()

message(STATUS "MMS: install package -> ${CMAKE_INSTALL_PREFIX}/${MMS_INSTALL_CMAKEDIR}")
