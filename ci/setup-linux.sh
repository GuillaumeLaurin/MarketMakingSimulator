#!/usr/bin/env bash
# Install the build / lint toolchain on Debian-Ubuntu and make sure a vcpkg
# checkout is available. Idempotent; safe to run locally.
. "$(dirname "$0")/lib.sh"

if have apt-get; then
    log "installing toolchain via apt-get"
    sudo apt-get update -qq
    sudo apt-get install -y --no-install-recommends \
        cmake ninja-build g++ clang clang-format clang-tidy gcovr \
        doxygen graphviz \
        pkg-config curl zip unzip tar ca-certificates git
fi

if [ -z "${VCPKG_ROOT:-}" ] || [ ! -x "${VCPKG_ROOT}/vcpkg" ]; then
    log "bootstrapping vcpkg into ./vcpkg"
    # Full commit history (blobs fetched lazily) so vcpkg can resolve any
    # builtin-baseline pinned in vcpkg.json; --depth 1 drops the baseline commit.
    [ -d vcpkg ] || git clone --filter=blob:none https://github.com/microsoft/vcpkg.git vcpkg
    ./vcpkg/bootstrap-vcpkg.sh -disableMetrics
    export_env VCPKG_ROOT "$(pwd)/vcpkg"
fi

log "cmake  : $(cmake --version | head -1)"
log "ninja  : $(ninja --version)"
log "vcpkg  : ${VCPKG_ROOT}"
