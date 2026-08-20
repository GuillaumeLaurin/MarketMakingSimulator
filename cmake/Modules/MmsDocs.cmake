# ==============================================================================
#  MmsDocs.cmake   (Modules: build-only)
#  Adds the "docs" target: Doxygen API documentation for the whole project.
#
#      cmake --preset dev -DMMS_BUILD_DOCS=ON
#      cmake --build --preset dev --target docs
#      # open build/dev/docs/html/index.html
#
#  Included from the top-level CMakeLists.txt only when MMS_BUILD_DOCS is ON.
#  A missing Doxygen is a warning, not an error.
# ==============================================================================

include_guard(GLOBAL)

find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(NOT DOXYGEN_FOUND)
    message(WARNING
        "MMS_BUILD_DOCS is ON but Doxygen was not found; the 'docs' target is unavailable.")
    return()
endif()

# --- values injected into docs/Doxyfile.in --------------------------------
set(DOXYGEN_OUTPUT_DIR "${PROJECT_BINARY_DIR}/docs")

if(TARGET Doxygen::dot)
    set(DOXYGEN_HAVE_DOT "YES")
else()
    set(DOXYGEN_HAVE_DOT "NO")
endif()

# Document the public API and the prose, not the tests / benchmarks / generated code.
set(_doc_inputs
    "${PROJECT_SOURCE_DIR}/docs/index.md"
    "${PROJECT_SOURCE_DIR}/CODING_STYLE.md"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/gen")            # generated <mms/version.hpp>
foreach(_name IN LISTS MMS_MODULE_NAMES)
    list(APPEND _doc_inputs "${PROJECT_SOURCE_DIR}/modules/${_name}/include")
    list(APPEND _doc_inputs "${PROJECT_SOURCE_DIR}/modules/${_name}/src")
endforeach()
list(JOIN _doc_inputs " " DOXYGEN_INPUT)

# Strip these prefixes from #include paths shown in the docs.
set(_strip_inc "${PROJECT_BINARY_DIR}/gen")
foreach(_name IN LISTS MMS_MODULE_NAMES)
    string(APPEND _strip_inc " ${PROJECT_SOURCE_DIR}/modules/${_name}/include")
endforeach()
set(DOXYGEN_STRIP_INC "${_strip_inc}")

# Let Doxygen see the export macros as empty so signatures stay readable.
set(DOXYGEN_PREDEFINED "DOXYGEN_RUNNING=1")
foreach(_name IN LISTS MMS_MODULE_NAMES)
    string(TOUPPER "${_name}" _uc)
    string(APPEND DOXYGEN_PREDEFINED " MMS_${_uc}_EXPORT= MMS_${_uc}_NO_EXPORT=")
endforeach()

configure_file("${PROJECT_SOURCE_DIR}/docs/Doxyfile.in"
               "${PROJECT_BINARY_DIR}/Doxyfile" @ONLY)

add_custom_target(docs
    COMMAND Doxygen::doxygen "${PROJECT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generating API documentation -> ${DOXYGEN_OUTPUT_DIR}/html/index.html"
    VERBATIM)

# Optional install (never part of the default install):
#   cmake --install <build> --component docs
if(MMS_INSTALL)
    install(DIRECTORY "${DOXYGEN_OUTPUT_DIR}/html/"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/api"
        COMPONENT docs
        EXCLUDE_FROM_ALL
        OPTIONAL)
endif()

message(STATUS "MMS: 'docs' target available (Doxygen ${DOXYGEN_VERSION}, dot=${DOXYGEN_HAVE_DOT})")
