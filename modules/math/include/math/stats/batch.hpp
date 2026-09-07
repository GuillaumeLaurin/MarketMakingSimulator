#ifndef MMS_MATH_STATS_BATCH_HPP
#define MMS_MATH_STATS_BATCH_HPP

/// \file
/// Exact statistics over a stored series (post-run / sweep analysis), on
/// std::span<const double>. Distinct from the O(1) accumulators: these take the
/// whole series and are free to sort or two-pass. An empty or too-short span
/// yields NaN. All ratios are per-period, NOT annualized (that is an mms
/// policy).

#include <span>
#include <vector>

#include <math/math_export.hpp>

namespace mms::math::stats {

/// Arithmetic mean. NaN for an empty span.
[[nodiscard]] MMS_MATH_EXPORT double mean(std::span<const double> xs) noexcept;

/// Sum of squared deviations divided by n. NaN for an empty span.
[[nodiscard]] MMS_MATH_EXPORT double population_variance(std::span<const double> xs) noexcept;

/// Sum of squared deviations divided by n - 1. NaN for an empty span.
[[nodiscard]] MMS_MATH_EXPORT double sample_variance(std::span<const double> xs) noexcept;

/// sqrt(sample_variance)
[[nodiscard]] MMS_MATH_EXPORT double stddev(std::span<const double> xs) noexcept;

/// Type-7 quantile. Sorts a copy. NaN for an empty span or p outside [0, 1].
[[nodiscard]] MMS_MATH_EXPORT double quantile(std::span<const double> xs, double p);

/// Type-7 quantile for each p, sorting the copy once. Result has ps.size()
/// entries; each is NaN if xs is empty or that p is out of range.
[[nodiscard]] MMS_MATH_EXPORT std::vector<double> quantiles(std::span<const double> xs,
                                                            std::span<const double> ps);

/// Largest peak-to-trough drop (absolute) of an equity curve. NaN for an empty
/// span.
[[nodiscard]] MMS_MATH_EXPORT double max_drawdown(std::span<const double> equity) noexcept;

/// (mean(returns) - risk_free) / stddev(returns), per period. NaN for fewer
/// than 2 values or a zero standard deviation.
[[nodiscard]] MMS_MATH_EXPORT double sharpe_ratio(std::span<const double> returns,
                                                  double risk_free = 0.0) noexcept;

/// (mean(returns) - risk_free) / downside_deviation, per period, where the
/// downside deviation is sqrt(mean(min(r - risk_free, 0)^2)). NaN for an empty
/// span or when no return falls below risk_free.
[[nodiscard]] MMS_MATH_EXPORT double sortino_ratio(std::span<const double> returns,
                                                   double risk_free = 0.0) noexcept;

} // namespace mms::math::stats

#endif // MMS_MATH_STATS_BATCH_HPP