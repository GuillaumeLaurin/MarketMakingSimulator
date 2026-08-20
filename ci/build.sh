#!/usr/bin/env bash
# Configure, build and (if it has a test preset) test one CMake preset.
#
#   ci/build.sh [preset]        preset defaults to "dev"
#
# Runs identically on a dev machine and in CI. Requires: cmake, ninja, a C++20
# toolchain, and $VCPKG_ROOT pointing at a vcpkg checkout.
. "$(dirname "$0")/lib.sh"

preset="${1:-dev}"
cd "$(repo_root)"

[ -n "${VCPKG_ROOT:-}" ] || die "VCPKG_ROOT is not set (run ci/setup-<os> first)"

log "configure  → preset '${preset}'"
cmake --preset "${preset}"

log "build      → preset '${preset}'"
cmake --build --preset "${preset}"

if ctest --list-presets 2>/dev/null | grep -q "\"${preset}\""; then
    log "test       → preset '${preset}'"
    ctest --preset "${preset}"
else
    log "test       → no test preset for '${preset}', skipping"
fi

log "done"
