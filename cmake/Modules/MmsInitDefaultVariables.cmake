# ==============================================================================
#  MmsInitDefaultVariables.cmake   (Modules: build-only)
#  Analogue of TinyORM's cmake/Modules/TinyInitDefaultVariables.cmake
#
#  mms_init_default_variables()  --  a macro (runs in the caller's scope) that
#  pins the default CMake variables the whole build relies on. Call it right
#  after project() in the top-level CMakeLists.txt.
# ==============================================================================

include_guard(GLOBAL)

macro(mms_init_default_variables)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
        message(FATAL_ERROR
            "In-source builds are not allowed. Configure from a separate directory, "
            "e.g.  cmake -S . -B build")
    endif()

    # -- language / ABI ---------------------------------------------------------
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    # Windows resource compiler for the per-target VERSIONINFO (see
    # MmsResourceAndManifest.cmake). enable_language() must run at file scope,
    # which is why this lives in a macro rather than a function.
    if(WIN32 AND MMS_VERSION_RESOURCE)
        enable_language(RC)
    endif()

    # -- default build type (single-config generators only) --------------------
    get_property(_mms_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(NOT _mms_multi AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            Debug Release RelWithDebInfo MinSizeRel)
    endif()
    set(CMAKE_DEBUG_POSTFIX "d" CACHE STRING "Filename suffix for Debug libraries")

    # -- one output tree so the exe finds its module libs in the build tree ----
    mms_set_output_directories()

    # -- RPATH: locate sibling / ../lib module libraries after install --------
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
    include(GNUInstallDirs)
    file(RELATIVE_PATH _mms_bin_to_lib
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    if(APPLE)
        set(CMAKE_INSTALL_RPATH "@loader_path;@loader_path/${_mms_bin_to_lib}")
    elseif(UNIX)
        set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/${_mms_bin_to_lib}")
    endif()

    # Nicer nested configure output.
    list(APPEND CMAKE_MESSAGE_CONTEXT MMS)
endmacro()
