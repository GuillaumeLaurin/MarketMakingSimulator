#!/usr/bin/env bash
# Run clang-tidy (.clang-tidy) over the module / application translation units,
# using the compile database produced by the given preset (default: dev).
#
#   ci/check-tidy.sh [preset]
#
# Files are analysed in parallel (MMS_TIDY_JOBS, default: one per core).
#
# Set MMS_TIDY_BASE to a git ref (for example origin/main) to analyse only what
# changed since it, which is what pull requests do. A changed header re-checks
# every translation unit of its module, since any of them may include it. A
# change to the tidy config or to the CMake files that shape the compile
# database re-checks everything, as does an unset MMS_TIDY_BASE (pushes to
# main, local runs).
. "$(dirname "$0")/lib.sh"
cd "$(repo_root)"

have clang-tidy || die "clang-tidy not found (run ci/setup-linux.sh)"
preset="${1:-dev}"

log "configure  → preset '${preset}' (for compile_commands.json)"
cmake --preset "${preset}" >/dev/null

all_sources() {
    find modules src -type f \
        \( -name '*.cpp' -o -name '*.cxx' -o -name '*.cc' \) 2>/dev/null | sort
}

# Print the translation units to analyse, one per line.
select_sources() {
    if [ -z "${MMS_TIDY_BASE:-}" ]; then
        all_sources
        return
    fi

    local changed
    changed=$(git diff --name-only --diff-filter=ACMR "${MMS_TIDY_BASE}...HEAD") ||
        die "cannot diff against ${MMS_TIDY_BASE} (is it fetched? use fetch-depth: 0)"

    if printf '%s\n' "$changed" | grep -qE '(^|/)\.clang-tidy$|^CMakeLists\.txt$|^CMakePresets\.json$|^vcpkg\.json$|^cmake/'; then
        log "tidy config or build files changed: analysing everything" >&2
        all_sources
        return
    fi

    {
        # Changed translation units that still exist.
        printf '%s\n' "$changed" | grep -E '^(modules|src)/.*\.(cpp|cxx|cc)$' || true
        # Changed headers: every translation unit of the same module.
        printf '%s\n' "$changed" | grep -E '^modules/[^/]+/.*\.(hpp|hxx|h)$' |
            cut -d/ -f1-2 | sort -u |
            while read -r module; do
                find "$module" -type f \( -name '*.cpp' -o -name '*.cxx' -o -name '*.cc' \)
            done || true
        # Changed app headers: the app's translation units.
        if printf '%s\n' "$changed" | grep -qE '^(include|src)/.*\.(hpp|hxx|h)$'; then
            find src -type f \( -name '*.cpp' -o -name '*.cxx' -o -name '*.cc' \) 2>/dev/null
        fi
    } | sort -u | while read -r file; do
        [ -f "$file" ] && printf '%s\n' "$file"
    done
}

selected=$(select_sources)
mapfile -t files <<<"$selected"
[ -n "$selected" ] || files=()

if [ "${#files[@]}" -eq 0 ]; then
    log "no C/C++ sources to analyse, nothing to tidy"
    exit 0
fi

jobs="${MMS_TIDY_JOBS:-$(nproc 2>/dev/null || echo 2)}"
log "clang-tidy on ${#files[@]} file(s), ${jobs} at a time"
clang-tidy --version

# One clang-tidy process per file, ${jobs} at a time. xargs exits non-zero if
# any invocation does, so a compile error still fails the step. --quiet drops
# the per-file "N warnings generated" noise (almost all of it suppressed
# diagnostics from third-party headers).
printf '%s\n' "${files[@]}" |
    xargs -P "${jobs}" -n 1 clang-tidy --quiet -p "build/${preset}"
log "tidy OK"
