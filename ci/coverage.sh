#!/usr/bin/env bash
# Build the 'coverage' preset, run the tests, and emit reports under
# build/coverage/ : coverage.xml (Cobertura) + html/index.html + a console summary.
. "$(dirname "$0")/lib.sh"
cd "$(repo_root)"

have gcovr || die "gcovr not found (run ci/setup-linux.sh)"
[ -n "${VCPKG_ROOT:-}" ] || die "VCPKG_ROOT is not set"

cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage || true   # no tests yet -> not a failure

mkdir -p build/coverage/html

# Scope gcov data discovery to this build tree only. Add --fail-under-line <N>
# once there are tests, to make the job enforce a minimum.
gcovr build/coverage \
      --root . \
      --filter 'modules/' --filter 'src/' \
      --exclude '.*/(tests|bench)/.*' \
      --exclude '.*/gen/.*' \
      --exclude-throw-branches --exclude-unreachable-branches \
      --xml-pretty --xml build/coverage/coverage.xml \
      --html-details build/coverage/html/index.html \
      --print-summary \
    || log "gcovr: no coverage data yet (expected while the project is a skeleton)"
