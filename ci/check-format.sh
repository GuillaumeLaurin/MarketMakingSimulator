#!/usr/bin/env bash
# Fail if any tracked C/C++ source is not clang-format-clean (.clang-format).
. "$(dirname "$0")/lib.sh"
cd "$(repo_root)"

have clang-format || die "clang-format not found (run ci/setup-linux.sh)"

mapfile -t files < <(find modules src include -type f \
    \( -name '*.hpp' -o -name '*.hxx' -o -name '*.h' \
    -o -name '*.cpp' -o -name '*.cxx' -o -name '*.cc' \) 2>/dev/null | sort)

if [ "${#files[@]}" -eq 0 ]; then
    log "no C/C++ sources yet, nothing to format-check"
    exit 0
fi

log "clang-format --dry-run --Werror on ${#files[@]} file(s)"
clang-format --version
clang-format --dry-run --Werror "${files[@]}"
log "format OK"
