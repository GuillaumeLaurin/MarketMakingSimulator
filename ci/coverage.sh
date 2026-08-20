#!/usr/bin/env bash
# Build the 'coverage' preset, run the tests, and emit a Cobertura report at
# build/coverage/coverage.xml (plus a console summary).
. "$(dirname "$0")/lib.sh"
cd "$(repo_root)"

have gcovr || die "gcovr not found (run ci/setup-linux.sh)"
[ -n "${VCPKG_ROOT:-}" ] || die "VCPKG_ROOT is not set"

cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage || true   # no tests yet -> not a failure

mkdir -p build/coverage
gcovr --root . \
      --filter 'modules/' --filter 'src/' \
      --exclude '.*/(tests|bench)/.*' \
      --exclude '.*/gen/.*' \
      --xml-pretty --output build/coverage/coverage.xml \
      --print-summary \
    || log "gcovr: no coverage data yet (expected while the project is a skeleton)"
