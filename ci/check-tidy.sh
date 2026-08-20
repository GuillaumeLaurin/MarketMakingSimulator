#!/usr/bin/env bash
# Run clang-tidy (.clang-tidy) over the module / application translation units,
# using the compile database produced by the given preset (default: dev).
#
#   ci/check-tidy.sh [preset]
. "$(dirname "$0")/lib.sh"
cd "$(repo_root)"

have clang-tidy || die "clang-tidy not found (run ci/setup-linux.sh)"
preset="${1:-dev}"

log "configure  → preset '${preset}' (for compile_commands.json)"
cmake --preset "${preset}" >/dev/null

mapfile -t files < <(find modules src -type f \
    \( -name '*.cpp' -o -name '*.cxx' -o -name '*.cc' \) 2>/dev/null | sort)

if [ "${#files[@]}" -eq 0 ]; then
    log "no C/C++ sources yet, nothing to tidy"
    exit 0
fi

log "clang-tidy on ${#files[@]} file(s)"
clang-tidy --version
clang-tidy -p "build/${preset}" "${files[@]}"
log "tidy OK"
