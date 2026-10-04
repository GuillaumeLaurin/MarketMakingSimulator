# ==============================================================================
#  StageRuntimeDlls.cmake   (script mode: cmake -P)
#  Copies the runtime DLLs of one test executable next to it, under a lock.
#
#    cmake -DDLLS=<a>*<b>*... -DDEST=<dir> -P StageRuntimeDlls.cmake
#
#  Every test executable stages the same module DLLs into the same directory
#  from its own POST_BUILD step, and Ninja links many of them in parallel. On
#  Windows a DLL being copied by one step cannot be opened by another
#  ("open_for_read: permission denied"), which failed the MSVC CI build. The
#  lock turns the copies into a queue; copy_if_different keeps every one after
#  the first nearly free. '*' separates the paths because it can neither occur
#  in a file name nor be mistaken for a shell operator by cmd.exe.
# ==============================================================================

if(NOT DLLS)
    return() # ELF/Mach-O platforms use RPATH: nothing to stage
endif()

string(REPLACE "*" ";" _dlls "${DLLS}")
file(MAKE_DIRECTORY "${DEST}")

file(LOCK "${DEST}/.stage_runtime_dlls.lock" GUARD PROCESS TIMEOUT 300 RESULT_VARIABLE _locked)
if(NOT _locked EQUAL 0)
    message(FATAL_ERROR "StageRuntimeDlls: could not lock ${DEST} (${_locked})")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_dlls} "${DEST}"
    RESULT_VARIABLE _copied)
if(NOT _copied EQUAL 0)
    message(FATAL_ERROR "StageRuntimeDlls: copying to ${DEST} failed (${_copied})")
endif()
