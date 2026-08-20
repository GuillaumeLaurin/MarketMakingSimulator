# ==============================================================================
#  MmsResourceAndManifest.cmake   (CommonModules)
#  Analogue of TinyORM's cmake/CommonModules/TinyResourceAndManifest.cmake
#
#  mms_resource_and_manifest(<target>
#      [NAME <display-name>]           default: target's OUTPUT_NAME
#      [TYPE Library|Executable])      default: derived from the target kind
#
#  On Windows only: generates a VERSIONINFO resource script filled with the
#  project version and attaches it to <target> so the produced .dll / .exe
#  carries proper file-version metadata. No-op everywhere else, and a no-op when
#  MMS_VERSION_RESOURCE is OFF or the RC language is unavailable.
# ==============================================================================

include_guard(GLOBAL)

# Bracket string -> configured per target with string(CONFIGURE ... @ONLY).
set(_MMS_RC_TEMPLATE [[
#include <winver.h>

VS_VERSION_INFO VERSIONINFO
 FILEVERSION    @MMS_RC_VER_COMMA@
 PRODUCTVERSION @MMS_RC_VER_COMMA@
 FILEFLAGSMASK  VS_FFI_FILEFLAGSMASK
 FILEFLAGS      @MMS_RC_FILEFLAGS@
 FILEOS         VOS_NT_WINDOWS32
 FILETYPE       @MMS_RC_FILETYPE@
 FILESUBTYPE    VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName",      "@MMS_RC_COMPANY@"
            VALUE "FileDescription",  "@MMS_RC_NAME@"
            VALUE "FileVersion",      "@MMS_RC_VER_DOT@"
            VALUE "InternalName",     "@MMS_RC_NAME@"
            VALUE "OriginalFilename", "@MMS_RC_FILENAME@"
            VALUE "ProductName",      "@MMS_RC_PRODUCT@"
            VALUE "ProductVersion",   "@MMS_RC_VER_DOT@"
            VALUE "LegalCopyright",   "@MMS_RC_COPYRIGHT@"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
]])

function(mms_resource_and_manifest target)
    if(NOT WIN32 OR NOT MMS_VERSION_RESOURCE)
        return()
    endif()
    get_property(_rc_enabled GLOBAL PROPERTY ENABLED_LANGUAGES)
    if(NOT "RC" IN_LIST _rc_enabled)
        message(STATUS "MMS: RC language not enabled -- skipping version resource for ${target}")
        return()
    endif()

    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "NAME;TYPE" "")

    get_target_property(_type ${target} TYPE)
    get_target_property(_outname ${target} OUTPUT_NAME)
    if(NOT _outname)
        set(_outname "${target}")
    endif()
    if(NOT ARG_NAME)
        set(ARG_NAME "${_outname}")
    endif()
    if(NOT ARG_TYPE)
        if(_type STREQUAL "EXECUTABLE")
            set(ARG_TYPE "Executable")
        else()
            set(ARG_TYPE "Library")
        endif()
    endif()

    # Version pieces (pad the 4th field with 0).
    set(_v "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    set(MMS_RC_VER_COMMA "${_v}")
    set(MMS_RC_VER_DOT   "${PROJECT_VERSION}")
    set(MMS_RC_NAME      "${ARG_NAME}")
    set(MMS_RC_PRODUCT   "${PROJECT_NAME}")
    set(MMS_RC_COMPANY   "${PROJECT_NAME} project")
    set(MMS_RC_COPYRIGHT "MIT License")
    if(ARG_TYPE STREQUAL "Executable")
        set(MMS_RC_FILETYPE "VFT_APP")
        set(MMS_RC_FILENAME "${_outname}.exe")
    else()
        set(MMS_RC_FILETYPE "VFT_DLL")
        set(MMS_RC_FILENAME "${_outname}.dll")
    endif()
    set(MMS_RC_FILEFLAGS "0x0L")

    string(CONFIGURE "${_MMS_RC_TEMPLATE}" _rc_body @ONLY)
    set(_rc_file "${CMAKE_CURRENT_BINARY_DIR}/gen/${target}_version.rc")
    file(GENERATE OUTPUT "${_rc_file}" CONTENT "${_rc_body}")

    target_sources(${target} PRIVATE "${_rc_file}")
endfunction()
