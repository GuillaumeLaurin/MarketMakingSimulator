# MMS coding style

This document is the reference for how code in this repository is written. The
mechanical parts (spacing, include order, brace placement) are enforced by tools;
this file explains the conventions those tools cannot express and the reasoning
behind them.

## 1. Tooling is authoritative

| Concern | Enforced by | How to run |
|---|---|---|
| Formatting | [`.clang-format`](../.clang-format) | `ci/check-format.sh` (CI fails on any diff) |
| Static analysis | [`.clang-tidy`](../.clang-tidy) | `ci/check-tidy.sh dev` (informational for now) |
| Warnings | `MmsCommon.cmake` (`-Wall -Wextra -Wpedantic` and more) | every build; CI uses `-Werror` |

Never hand-format code to fight clang-format, and never silence a warning
without a comment that explains why the warning is wrong here.

## 2. Language

* C++20, no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`).
* Prefer standard-library facilities over hand-rolled ones.
* `enum class` only; never a bare `enum`.
* No raw `new` / `delete`. Ownership is expressed with `std::unique_ptr`; shared
  ownership needs a written justification.
* No `using namespace` at file or namespace scope in a header. It is acceptable
  inside a function body or a `.cpp`.

## 3. Naming

| Kind | Convention | Example |
|---|---|---|
| Namespace | lower_snake | `mms`, `mms::core`, `mms::detail` |
| Type (class, struct, enum, alias) | PascalCase | `OrderBook`, `SimulationConfig`, `Price` |
| Function, method, variable | lower_snake | `best_bid()`, `on_fill()`, `quote_size` |
| Private / protected data member | trailing underscore | `price_`, `params_` |
| Compile-time constant value | lower_snake | `inline constexpr int version_major` |
| Macro | UPPER_SNAKE, `MMS_` prefix | `MMS_VERSION_MAJOR`, `MMS_CORE_EXPORT` |
| File | lower_snake, `.hpp` / `.cpp` | `order_book.hpp` |
| CMake function | lower_snake, `mms_` prefix | `mms_add_module()` |
| CMake target | `mms_<name>` with alias `MMS::<name>` | `mms_core` / `MMS::core` |

The trailing-underscore member convention exists so a constructor parameter can
share the conceptual name without triggering `-Wshadow`.

## 4. Headers

* Include guard, not `#pragma once` (which is non-standard and not portably
  guaranteed). The first two lines are `#ifndef <GUARD>` / `#define <GUARD>`, the
  last is `#endif // <GUARD>`. The guard macro is the header path under
  `include/` uppercased with `/` and `.` turned into `_`: `math/rng/philox.hpp`
  becomes `MMS_MATH_RNG_PHILOX_HPP`.
* Public headers live in `modules/<name>/include/<name>/`; private headers next
  to the sources in `modules/<name>/src/`. A public header must never include a
  private one.
* Each module ships an umbrella header `<name>/<name>.hpp` that includes its
  public API.
* Include order (clang-format regroups automatically):
  1. the standard library, `<vector>`, `<optional>`, ...
  2. third-party, `<fmt/format.h>`, `<Eigen/Dense>`, ...
  3. this project's modules, `<core/order_book.hpp>`, ...
  4. local quoted includes, `"reservation_price.hpp"`.
* Keep one primary type per header. Small closely-related helpers may share one.

## 5. Symbol visibility

Symbols are hidden by default. Every class or free function that is part of a
module's public ABI is tagged with that module's export macro, taken from the
generated `<name>/<name>_export.hpp`:

```cpp
#include <core/core_export.hpp>

class MMS_CORE_EXPORT OrderBook { /* ... */ };

MMS_CORE_EXPORT std::optional<Price> mid_price(const OrderBook& book);
```

Templates, `inline` functions and purely header-side code are not tagged.

## 6. API design

* `[[nodiscard]]` on any function whose result is the reason to call it
  (observers, factories, `try_*`).
* `noexcept` when the function genuinely cannot throw. Do not add it
  speculatively.
* Configuration and data-transfer structs are plain aggregates; fill them with
  designated initializers. Types with an invariant get real constructors.
* Pass small trivially-copyable types by value (`Price`, `Order`); pass larger
  types by `const&`. Take a `T&&` sink parameter only when you store it.
* Hide heavy dependencies behind the pimpl idiom (see `Simulation`).
* Expected "absence" is `std::optional` (`OrderBook::best_bid()`); an operation
  that may legitimately do nothing returns `bool` (`OrderBook::cancel()`).

## 7. Error handling

* Throw for programmer errors and for construction that cannot establish an
  invariant.
* Return a value (`std::optional`, `bool`, a result struct) for outcomes that
  are part of normal control flow.
* Never throw across the simulation tick loop for an expected condition.

## 8. Module boundaries

Dependency direction, strictly one way:

```
app  ->  MMS::simulation  ->  MMS::mm  ->  { MMS::core, MMS::math }
```

* A module may only use another module listed in its `PUBLIC_DEPENDS` /
  `PRIVATE_DEPENDS`.
* `MMS::mm` and `MMS::simulation` must not include any CLI, file-I/O or UI
  header. That code belongs to the application layer (`src/`).
* Third-party libraries are used only through the imported targets declared in
  `cmake/Modules/MmsDependencies.cmake`.

## 9. Tests and benchmarks

* Unit tests: Catch2 (`TEST_CASE` / `SECTION`), trompeloeil for mocks. One file
  per unit under `modules/<name>/tests/`; the directory is glob-picked, no
  `CMakeLists.txt` editing required.
* Benchmarks: nanobench under `modules/<name>/bench/`, same glob mechanism. The
  order-book container comparison (`std::map` vs `std::unordered_map` vs
  `std::deque`) belongs in `modules/core/bench/`.
* A bug fix comes with a test that fails without the fix.

## 10. Comments and documentation

* Doxygen `///` for every public declaration: a one-line brief, then `@param` /
  `@return` when not obvious. `//` for implementation notes inside `.cpp`.
* Comment the "why", not the "what". The code already says what it does.
* Do not use em-dashes. Use a comma, a colon, parentheses, or two sentences.
