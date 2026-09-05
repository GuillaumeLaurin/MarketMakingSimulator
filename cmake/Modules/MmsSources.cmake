# ==============================================================================
#  MmsSources.cmake   (Modules: build-only)
#  Analogue of TinyORM's cmake/Modules/TinySources.cmake -- centralises how a
#  module's translation units and public headers are collected.
#
#  mms_collect_module_sources(<name> <src-out-var> <hdr-out-var>
#      [SOURCES <explicit file>...]      bypass the glob
#      [HEADERS <explicit file>...])
#
#  Default behaviour: recursive glob of src/<name>/ and include/<name>/ with
#  CONFIGURE_DEPENDS. When the module has no implementation file yet, a small
#  placeholder .cpp is generated so the library still links.
# ==============================================================================

include_guard(GLOBAL)

function(mms_collect_module_sources name src_out hdr_out)
    cmake_parse_arguments(PARSE_ARGV 3 ARG "" "" "SOURCES;HEADERS")

    set(_src_dir "${PROJECT_SOURCE_DIR}/modules/${name}/src")
    set(_inc_dir "${PROJECT_SOURCE_DIR}/modules/${name}/include/${name}")

    set(_sources ${ARG_SOURCES})
    if(NOT _sources)
        file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
            "${_src_dir}/*.cpp" "${_src_dir}/*.cxx" "${_src_dir}/*.cc")
    endif()

    set(_headers ${ARG_HEADERS})
    if(NOT _headers)
        file(GLOB_RECURSE _headers CONFIGURE_DEPENDS
            "${_inc_dir}/*.hpp" "${_inc_dir}/*.hxx" "${_inc_dir}/*.h")
    endif()

    if(NOT _sources)
        set(_stub "${CMAKE_CURRENT_BINARY_DIR}/gen/${name}_placeholder.cpp")
        if(NOT EXISTS "${_stub}")
            string(TOUPPER "${name}" _name_uc)
            file(WRITE "${_stub}"
                "// Auto-generated: keeps the '${name}' module linkable until\n"
                "// src/${name}/ has real code. Safe to leave; it disappears from the\n"
                "// build as soon as a real .cpp exists.\n"
                "//\n"
                "// The dllexport gives MSVC an import library even while the module\n"
                "// has no real API: a Windows DLL with zero exports produces no .lib,\n"
                "// which breaks anything that links this module.\n"
                "#if defined(_WIN32) && !defined(MMS_${_name_uc}_STATIC_DEFINE)\n"
                "#  define MMS_${_name_uc}_PLACEHOLDER_EXPORT __declspec(dllexport)\n"
                "#else\n"
                "#  define MMS_${_name_uc}_PLACEHOLDER_EXPORT\n"
                "#endif\n"
                "namespace mms::${name}::detail {\n"
                "MMS_${_name_uc}_PLACEHOLDER_EXPORT int placeholder = 0;\n"
                "} // namespace mms::${name}::detail\n")
        endif()
        set(_sources "${_stub}")
    endif()

    set(${src_out} "${_sources}" PARENT_SCOPE)
    set(${hdr_out} "${_headers}" PARENT_SCOPE)
endfunction()
