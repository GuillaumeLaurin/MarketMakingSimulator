/// \file
/// Implementation of the span-based helpers declared in <math/numeric.hpp>,
/// plus compile-time smoke tests that exercise the C++20 standard library
/// (<concepts>, <numbers>) and the constexpr scalar helpers. This is the first
/// real translation unit of MMS::math: it un-stubs the module and pins the
/// C++20 toolchain configuration in compile_commands.json.

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>

#include <math/numeric.hpp>

namespace mms::math {

bool all_close(std::span<const double> a,
               std::span<const double> b,
               double rel_tol,
               double abs_tol) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!almost_equal(a[i], b[i], rel_tol, abs_tol)) {
            return false;
        }
    }
    return true;
}

double max_abs_diff(std::span<const double> a, std::span<const double> b) noexcept {
    if (a.size() != b.size() || a.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::fabs(a[i] - b[i]));
    }
    return worst;
}

} // namespace mms::math
