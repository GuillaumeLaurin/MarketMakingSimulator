# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Modular CMake build: one dynamic library per module (`MMS::core`, `MMS::math`,
  `MMS::mms`, `MMS::simulation`), a `cmake/` toolbox (`CommonModules` +
  `Modules`), generated per-module export headers, and a `find_package(MMS)`
  package config.
- vcpkg manifest (`vcpkg.json`) with a pinned baseline, and `CMakePresets.json`
  (`dev`, `release`, `asan`, `coverage`, plus `*-mingw` variants).
- Continuous integration (`.github/workflows/ci.yml`): build matrix (Linux
  GCC/Clang, Windows MSVC), lint, sanitizers, coverage and Doxygen, gated so the
  pipeline stays idle until the modules contain sources.
- `ci/` scripts mirroring the CI commands for local use.
- Coding style guide (`CODING_STYLE.md`), contribution guide (`CONTRIBUTING.md`),
  issue and pull-request templates.
- Doxygen `docs` target (`MMS_BUILD_DOCS`).
- Editor configuration: `.clang-format`, `.clang-tidy`, `.editorconfig`, and a
  VS Code IntelliSense bootstrap script.

[Unreleased]: https://github.com/GuillaumeLaurin/MMS/commits/main
