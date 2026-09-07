# ==============================================================================
#  MmsTestCommon.cmake   (Modules: build-only)
#  Analogue of TinyORM's cmake/Modules/TinyTestCommon.cmake -- one helper to
#  declare a unit-test executable wired to CTest.
#
#  mms_add_test(<name>
#      SOURCES <file>...          the test translation unit(s)
#      [LINK   <target>...])       modules under test (MMS::core, ...)
#
#  Catch2 (with its bundled main) and trompeloeil are linked automatically;
#  both come from vcpkg via MmsDependencies.cmake when MMS_BUILD_TESTS is ON.
#  Catch2's CTest integration is used when available so each TEST_CASE shows up
#  as its own CTest entry.
# ==============================================================================

include_guard(GLOBAL)

if(TARGET Catch2::Catch2)
    include(Catch)   # provides catch_discover_tests(); shipped in Catch2's package
endif()

function(mms_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LINK")

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "mms_add_test(${name}): SOURCES is required")
    endif()

    set(_target "mms_test_${name}")
    add_executable(${_target} ${ARG_SOURCES})
    set_target_properties(${_target} PROPERTIES
        OUTPUT_NAME              "${name}"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out/test")

    target_link_libraries(${_target} PRIVATE
        "$<BUILD_INTERFACE:mms_common>"
        Catch2::Catch2WithMain
        trompeloeil::trompeloeil
        ${ARG_LINK})

    # Stage every runtime DLL the test needs next to the executable. On Windows
    # the loader searches the exe's own directory first, so this makes each test
    # self-contained regardless of PATH: it fixes catch_discover_tests launching
    # the binary during discovery, and plain `ctest` / a direct run. The genex
    # list is empty on ELF/Mach-O platforms (they use RPATH), so the command is a
    # no-op there.
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E
            "$<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${_target}>>,copy_if_different,true>"
            "$<TARGET_RUNTIME_DLLS:${_target}>" "$<TARGET_FILE_DIR:${_target}>"
        COMMAND_EXPAND_LISTS
        VERBATIM)

    if(COMMAND catch_discover_tests)
        # Modules are shared libraries by default (BUILD_SHARED_LIBS=ON), and the
        # test binary lives in out/test while the mms_<name>.dll it links lives in
        # out/bin. DL_PATHS makes both the discovery step and ctest prepend
        # out/bin to the loader path; PRE_TEST defers discovery to test time so a
        # missing DLL never breaks the build.
        catch_discover_tests(${_target}
            DL_PATHS "${PROJECT_BINARY_DIR}/out/bin"
            DISCOVERY_MODE PRE_TEST)
    else()
        add_test(NAME ${name} COMMAND ${_target})
    endif()
endfunction()
