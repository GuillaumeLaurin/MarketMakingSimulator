#ifndef MMS_MATH_STATS_EWMA_HPP
#define MMS_MATH_STATS_EWMA_HPP

/// \file
/// EwmaEstimator: exponentially weighted moving mean and variance. The window is
/// set by a half-life expressed in NUMBER OF add() calls (the estimator sees no
/// dt; the caller must feed equally spaced samples). lambda = exp(-ln2 /
/// half_life) is the per-step decay. No bias correction: the first ~3 half-lives
/// are a warmup.

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace mms::math::stats {

namespace detail {
inline constexpr double ewma_nan = std::numeric_limits<double>::quiet_NaN();
} // namespace detail

class EwmaEstimator {
public:
    explicit EwmaEstimator(double half_life)
        : half_life_(half_life), lambda_(std::exp(-std::numbers::ln2 / half_life)) {
        if (!std::isfinite(half_life) || half_life <= 0.0) {
            throw std::invalid_argument("EwmaEstimator: require finite half_life > 0");
        }
    }

    /// Fold one observation in. A non-finite x is ignored (no-op).
    void add(double x) noexcept {
        if (!std::isfinite(x)) {
            return;
        }
        if (count_ == 0) {
            mean_ = x;
            var_ = 0.0;
            count_ = 1;
            return;
        }

        ++count_;
        const double d = x - mean_;
        mean_ += (1.0 - lambda_) * d;
        var_ = (lambda_ * var_) + ((1.0 - lambda_) * d * d);
    }

    [[nodiscard]] std::int64_t sample_count() const noexcept { return count_; }

    [[nodiscard]] double half_life() const noexcept { return half_life_; }

    [[nodiscard]] double mean() const noexcept { return count_ == 0 ? detail::ewma_nan : mean_; }

    /// EWMA variance. 0 after the first sample (no bias correction); NaN before it.
    [[nodiscard]] double variance() const noexcept { return count_ == 0 ? detail::ewma_nan : var_; }

    [[nodiscard]] double stddev() const noexcept { return std::sqrt(variance()); }

private:
    double half_life_;
    double lambda_;
    std::int64_t count_ = 0;
    double mean_ = 0.0;
    double var_ = 0.0;
};

} // namespace mms::math::stats

#endif // MMS_MATH_STATS_EWMA_HPP