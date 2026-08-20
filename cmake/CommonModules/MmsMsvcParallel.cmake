# ==============================================================================
#  MmsMsvcParallel.cmake   (CommonModules)
#  Analogue of TinyORM's cmake/CommonModules/TinyMsvcParallel.cmake
#
#  mms_msvc_parallel(<description>)
#  Exposes the MMS_MSVC_PARALLEL cache entry and enables MSVC's /MP flag
#  (parallel compilation within a single target). No-op on non-MSVC toolchains.
#     ON            -> /MP        (compiler decides the thread count)
#     <integer N>   -> /MP<N>
#     OFF / empty   -> disabled
# ==============================================================================

include_guard(GLOBAL)

function(mms_msvc_parallel description)
    set(MMS_MSVC_PARALLEL "ON" CACHE STRING "${description}")

    if(NOT MSVC)
        return()
    endif()

    string(TOUPPER "${MMS_MSVC_PARALLEL}" _val)
    if(_val STREQUAL "OFF" OR _val STREQUAL "FALSE" OR _val STREQUAL "NO" OR _val STREQUAL "")
        return()
    elseif(_val STREQUAL "ON" OR _val STREQUAL "TRUE" OR _val STREQUAL "YES")
        add_compile_options($<$<COMPILE_LANGUAGE:CXX,C>:/MP>)
    elseif(MMS_MSVC_PARALLEL MATCHES "^[0-9]+$")
        add_compile_options($<$<COMPILE_LANGUAGE:CXX,C>:/MP${MMS_MSVC_PARALLEL}>)
    else()
        message(WARNING "MMS_MSVC_PARALLEL='${MMS_MSVC_PARALLEL}' is not ON/OFF/<integer> -- ignored")
    endif()
endfunction()
