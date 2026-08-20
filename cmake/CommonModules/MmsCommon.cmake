# ==============================================================================
#  MmsCommon.cmake  --  mms_common()   (CommonModules: installed & reusable)
#  Direct analogue of TinyORM's tiny_common(): an INTERFACE library carrying the
#  compile options / definitions every MMS target builds with. Modules link it
#  PRIVATE through $<BUILD_INTERFACE:...> so the flags never reach downstream
#  consumers of the installed package.
# ==============================================================================

include_guard(GLOBAL)

function(mms_common target)
    set(oneValueArgs NAMESPACE NAME)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "${oneValueArgs}" "")

    if(DEFINED ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "mms_common() got unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    add_library(${target} INTERFACE)
    if(ARG_NAMESPACE AND ARG_NAME)
        add_library(${ARG_NAMESPACE}::${ARG_NAME} ALIAS ${target})
        set_target_properties(${target} PROPERTIES EXPORT_NAME ${ARG_NAME})
    endif()

    target_compile_features(${target} INTERFACE cxx_std_20)

    # -- MSVC / clang-cl -------------------------------------------------------
    if(MSVC)
        target_compile_options(${target} INTERFACE
            /nologo /EHsc /utf-8
            /Zc:__cplusplus /Zc:preprocessor /permissive-
            /W4
            /wd4251 /wd4275              # dll-interface noise for STL members / bases
            $<$<BOOL:${MMS_WERROR}>:/WX>
        )
        target_compile_definitions(${target} INTERFACE
            WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
            $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
        )

    # -- GCC / Clang / AppleClang ------------------------------------------------
    else()
        target_compile_options(${target} INTERFACE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Wold-style-cast
            -Wcast-align -Wcast-qual -Wconversion -Wsign-conversion
            -Wdouble-promotion -Wnull-dereference -Wformat=2 -Wundef
            -pipe
            $<$<BOOL:${MMS_WERROR}>:-Werror>
        )
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT APPLE)
            target_link_options(${target} INTERFACE -fuse-ld=lld)
        endif()
    endif()

    # -- CI instrumentation --------------------------------------------------
    # Applied through this interface target, which every build target links as
    # $<BUILD_INTERFACE:mms_common>, so it never leaks into the installed package.
    if(MMS_COVERAGE AND NOT MSVC)
        target_compile_options(${target} INTERFACE --coverage -O0 -g)
        target_link_options(${target} INTERFACE --coverage)
    endif()

    if(MMS_SANITIZE)
        if(MSVC)
            target_compile_options(${target} INTERFACE /fsanitize=address)
        else()
            target_compile_options(${target} INTERFACE
                -fsanitize=${MMS_SANITIZE} -fno-omit-frame-pointer -g -O1)
            target_link_options(${target} INTERFACE -fsanitize=${MMS_SANITIZE})
        endif()
    endif()
endfunction()
