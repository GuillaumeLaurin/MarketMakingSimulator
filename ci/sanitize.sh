#!/usr/bin/env bash
# Build + test with sanitizers. Preset defaults to 'asan' (address + undefined).
#
#   ci/sanitize.sh [preset]
#
# The build itself is set to abort on the first finding (-fno-sanitize-recover),
# so a violation fails ctest. The options below only tune reporting.
#
# Multi-.so C++ note: if ASan reports a false "odr-violation" across the module
# libraries, add  detect_odr_violation=0  to ASAN_OPTIONS.
. "$(dirname "$0")/lib.sh"

export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:detect_leaks=1:check_initialization_order=1:detect_stack_use_after_return=1:strict_string_checks=1:detect_odr_violation=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
export LSAN_OPTIONS="${LSAN_OPTIONS:-}"

exec "$(dirname "$0")/build.sh" "${1:-asan}"
