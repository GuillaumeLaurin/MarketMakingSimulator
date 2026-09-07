#ifndef MMS_MATH_STATS_RUNNING_STATS_HPP
#define MMS_MATH_STATS_RUNNING_STATS_HPP

/// \file
/// RunningStats: single-pass mean / variance / min / max by West's weighted
/// Welford recursion. O(1) memory and per-update cost. Weights are reliability
/// weights (a weight of w means "worth w observations of confidence"), so the
/// unbiased sample variance divides by the effective degrees of freedom
/// Sw - Sw^2 / Sw. Copyable (mid-run snapshot); merge() combines two partial
/// accumulators (Chan's parallel formula).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace mms::math::stats {

namespace detail {
inline constexpr double running_stats_nan = std::numeric_limits<double>::quiet_NaN();
} // namespace detail

class RunningStats {
public:
    RunningStats() = default;

    /// Fold one observation in. A non-finite x or w, or w <= 0, is ignored
    /// (no-op): no throw on the hot path.
    void add(double x, double w = 1.0) noexcept {
        if (!std::isfinite(x) || !std::isfinite(w) || w <= 0.0) {
            return;
        }
        ++count_;
        w_sum_ += w;
        w_sum_sq_ += w * w;
        const double delta = x - mean_;
        mean_ += (w / w_sum_) * delta;
        m2_ += w * delta * (x - mean_);
        min_ = std::min(min_, x);
        max_ = std::max(max_, x);
    }

    /// Fold another accumulator's whole history in.
    void merge(const RunningStats& other) noexcept {
        if (other.count_ == 0) {
            return;
        }
        if (count_ == 0) {
            *this = other;
            return;
        }
        const double w = w_sum_ + other.w_sum_;
        const double delta = other.mean_ - mean_;
        mean_ += delta * (other.w_sum_ / w);
        m2_ += other.m2_ + ((delta * delta) * ((w_sum_ * other.w_sum_) / w));
        w_sum_ = w;
        w_sum_sq_ += other.w_sum_sq_;
        count_ += other.count_;
        min_ = std::min(min_, other.min_);
        max_ = std::max(max_, other.max_);
    }

    [[nodiscard]] std::int64_t count() const noexcept { return count_; }

    [[nodiscard]] double weight_sum() const noexcept { return w_sum_; }

    [[nodiscard]] double mean() const noexcept {
        return count_ == 0 ? detail::running_stats_nan : mean_;
    }

    /// m2 / Sw. Defined for count >= 1
    [[nodiscard]] double population_variance() const noexcept {
        return count_ == 0 ? detail::running_stats_nan : m2_ / w_sum_;
    }

    /// m2 / (Sw - Sw^2 / Sw). NaN for fewer than 2 samples or a non-positive
    /// effective degrees of freedom.
    [[nodiscard]] double sample_variance() const noexcept {
        if (count_ < 2 || w_sum_ <= 0.0) {
            return detail::running_stats_nan;
        }
        const double eff_dof = w_sum_ - (w_sum_sq_ / w_sum_);
        return eff_dof > 0.0 ? m2_ / eff_dof : detail::running_stats_nan;
    }

    [[nodiscard]] double stddev() const noexcept { return std::sqrt(sample_variance()); }

    [[nodiscard]] double min() const noexcept {
        return count_ == 0 ? detail::running_stats_nan : min_;
    }

    [[nodiscard]] double max() const noexcept {
        return count_ == 0 ? detail::running_stats_nan : max_;
    }

private:
    std::int64_t count_ = 0;
    double w_sum_ = 0.0;
    double w_sum_sq_ = 0.0;
    double mean_ = 0.0;
    double m2_ = 0.0;
    double min_ = std::numeric_limits<double>::infinity();
    double max_ = -std::numeric_limits<double>::infinity();
};

/// Pure combine: neither operand is modified.
[[nodiscard]] inline RunningStats merged(const RunningStats& a, const RunningStats& b) noexcept {
    RunningStats result = a;
    result.merge(b);
    return result;
}

} // namespace mms::math::stats

#endif // MMS_MATH_STATS_RUNNING_STATS_HPP