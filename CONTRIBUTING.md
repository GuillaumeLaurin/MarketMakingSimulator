# Contributing to Market Making Simulator

Contributions are welcome. This is a learning project, so clarity is valued over
cleverness, and small focused changes are preferred.

## 1. Set up

```bash
git clone https://github.com/<your-user>/MMS.git
cd MMS
git remote add upstream https://github.com/GuillaumeLaurin/MMS.git
export VCPKG_ROOT=/path/to/vcpkg          # or run ci/setup-linux.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

On an MSYS2 / MinGW-w64 UCRT64 toolchain, use `dev-mingw` instead of `dev`.
Prerequisites and the full list of presets are in the [README](README.md).

Keep your fork's `main` in sync and always branch from it:

```bash
git checkout main
git fetch upstream
git rebase upstream/main
git checkout -b feat/my-change
```

## 2. Reporting issues

- Search open and closed issues first.
- One issue per report: a single bug or a single request.
- Bug reports include what you did, what you expected, what happened, and the
  preset, compiler version and OS. Add a minimal reproduction when possible.
- Feature requests describe the use case and the "why", not only a proposed
  solution.
- Use a specific title. "Order book cancel leaves an empty price level" beats
  "bug in core".

The issue templates prompt for each of these.

## 3. Branches

Branch off `main`, named by intent:

| Prefix | For |
|---|---|
| `feat/` | a new capability |
| `fix/` | a bug fix |
| `refactor/` | internal change, no behavior change |
| `perf/` | a performance change |
| `test/` | tests only |
| `docs/` | documentation only |
| `ci/` | build or CI only |

## 4. Commit messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
type(scope): imperative summary, at most ~72 characters

Optional body explaining the "why", wrapped near 72 columns. Describe the
problem the change solves, not a restatement of the diff.

Refs #12
```

- `type`: `feat`, `fix`, `docs`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`
- `scope`: the affected area, `core`, `math`, `mms`, `simulation`, `app`,
  `cmake`, `ci`, `docs`
- Link issues with `Refs #12` or `Closes #12`.

Examples:

```
feat(core): add price-time priority to the matching engine
fix(mms): reset average price when the position flips sign
build(cmake): install the generated export headers per module
```

One logical change per commit. Do not mix reformatting or unrelated edits into a
functional commit.

## 5. Coding standards

- Read [`CODING_STYLE.md`](CODING_STYLE.md): naming, header layout, symbol
  visibility, module boundaries, error handling.
- Formatting and static analysis are configured by `.clang-format` and
  `.clang-tidy` at the repository root.

```bash
bash ci/check-format.sh          # must pass; CI fails on any diff
bash ci/check-tidy.sh dev        # advisory for now
```

- Respect the dependency direction: `app` then `MMS::simulation` then `MMS::mm`
  then `{ MMS::core, MMS::math }`. `MMS::mm` and `MMS::simulation` must not
  include any CLI, file-I/O or UI header.
- Third-party libraries are used only through the imported targets declared in
  `cmake/Modules/MmsDependencies.cmake`, after adding them to `vcpkg.json`.

## 6. Tests

- Unit tests use Catch2 (`TEST_CASE` / `SECTION`) with trompeloeil for mocks.
- Put one test file per unit under `modules/<name>/tests/`. The directory is
  glob-picked, no CMake edit is required.
- Any bug fix comes with a test that fails without the fix.
- Benchmarks (nanobench) go under `modules/<name>/bench/`, same mechanism.

## 7. Pull requests

- One PR per logical change. Rebase on the latest `upstream/main`; do not merge
  `main` into your branch.
- Before opening, verify locally:
  - `bash ci/check-format.sh`
  - `bash ci/build.sh dev`
- Fill in the PR template: what changed, why, the linked issue, how you tested.
- Update `CHANGELOG.md` under `[Unreleased]` for any user-facing change.
- All CI jobs must be green.
- Address review feedback with follow-up commits. The branch is squashed on
  merge, so the PR title becomes the commit subject: keep it in the
  Conventional Commits form.

## 8. License

By contributing, you agree that your contributions are licensed under the
project's [MIT License](LICENSE).
