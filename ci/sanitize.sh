#!/usr/bin/env bash
# Build + test with sanitizers. Preset defaults to 'asan' (address + undefined).
#
#   ci/sanitize.sh [preset]
. "$(dirname "$0")/lib.sh"

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=1:strict_string_checks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"

exec "$(dirname "$0")/build.sh" "${1:-asan}"
