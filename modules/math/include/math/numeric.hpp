#ifndef MMS_MATH_NUMERIC_HPP
#define MMS_MATH_NUMERIC_HPP

/// \file
/// Foundational numeric helpers for MMS::math: tolerant floating-point
/// comparison, guarded division, sign. Depends only on the standard library
/// (no Eigen, no other MMS::math header). The scalar helpers are constexpr and
/// header-only; the span helpers are compiled into the library.

#include <span>

#include <math/math_export.hpp>

namespace mms::math {

/// Default relative tolerance for cross-platform floating-point comparison.
/// The reproducibility plan (Target A) compares within this bound rather than
/// bit-for-bit across platforms and compilers.
inline constexpr double default_rel_tol = 1e-12;

/// Default absolute tolerance, used near zero where a relative bound collapses.
inline constexpr double default_abs_tol = 1e-14;

/// True when \p a and \p b agree within a combined bound:
/// `|a - b| <= max(rel_tol * max(|a|, |b|), abs_tol)`.
/// Exact equality (including matching infinities) short-circuits to true;
/// a NaN operand always compares unequal. Never throws.
[[nodiscard]] constexpr bool almost_equal(double a,
                                          double b,
                                          double rel_tol = default_rel_tol,
                                          double abs_tol = default_abs_tol) noexcept {
    if (a == b) {
        return true;
    }
    const double diff = (a < b) ? (b - a) : (a - b);
    const double abs_a = (a < 0.0) ? -a : a;
    const double abs_b = (b < 0.0) ? -b : b;
    const double scale = (abs_a > abs_b) ? abs_a : abs_b;
    const double bound = (rel_tol * scale > abs_tol) ? (rel_tol * scale) : abs_tol;
    return diff <= bound;
}

/// `num / den`, or \p fallback when `|den|` is below \p eps. The fallback is a
/// required argument: there is no universally sensible value for a division
/// that cannot be performed.
[[nodiscard]] constexpr double
safe_div(double num, double den, double fallback, double eps = default_abs_tol) noexcept {
    const double magnitude = (den < 0.0) ? -den : den;
    return (magnitude < eps) ? fallback : (num / den);
}

/// `-1`, `0` or `+1` following the sign of \p x. Both zeros and NaN map to `0`.
[[nodiscard]] constexpr int sign(double x) noexcept {
    return static_cast<int>(x > 0.0) - static_cast<int>(x < 0.0);
}

/// Element-wise ::mms::math::almost_equal over two ranges. A length mismatch
/// yields `false` rather than throwing, so the function is usable inside
/// assertions.
[[nodiscard]] MMS_MATH_EXPORT bool all_close(std::span<const double> a,
                                             std::span<const double> b,
                                             double rel_tol = default_rel_tol,
                                             double abs_tol = default_abs_tol) noexcept;

/// Largest element-wise `|a[i] - b[i]|`, for test diagnostics. Returns a NaN
/// when the lengths differ or either range is empty.
[[nodiscard]] MMS_MATH_EXPORT double max_abs_diff(std::span<const double> a,
                                                  std::span<const double> b) noexcept;

} // namespace mms::math

#endif // MMS_MATH_NUMERIC_HPP
