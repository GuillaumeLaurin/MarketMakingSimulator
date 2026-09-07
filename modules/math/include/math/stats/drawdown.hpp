#ifndef MMS_MATH_STATS_DRAWDOWN_HPP
#define MMS_MATH_STATS_DRAWDOWN_HPP

/// \file
/// DrawdownTracker: running peak, absolute and relative drawdown, and underwater
/// duration over an equity series fed one sample at a time. O(1) memory and
/// per-update cost. Absolute drawdown (peak - equity) is the primary measure;
/// relative drawdown is only defined once the peak is positive.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace mms::math::stats {

namespace detail {
inline constexpr double drawdown_nan = std::numeric_limits<double>::quiet_NaN();
} // namespace detail

class DrawdownTracker {
public:
    DrawdownTracker() = default;

    /// Fold one equity oberservation in. A non-finite value is ignored (no-op).
    void add(double equity) noexcept {
        if (!std::isfinite(equity)) {
            return;
        }
        ++count_;
        last_equity_ = equity;
        if (equity >= peak_) {
            peak_ = equity;
            current_underwater_ = 0;
        } else {
            ++current_underwater_;
            longest_underwater_ = std::max(longest_underwater_, current_underwater_);
        }
        const double abs_dd = peak_ - equity;
        max_abs_drawdown_ = std::max(max_abs_drawdown_, abs_dd);
        if (peak_ > 0.0) {
            max_rel_drawdown_ = std::max(max_rel_drawdown_, abs_dd / peak_);
        }
    }

    [[nodiscard]] std::int64_t count() const noexcept { return count_; }

    /// Highest equity seen so far. NaN before the first sample.
    [[nodiscard]] double peak() const noexcept {
        return count_ == 0 ? detail::drawdown_nan : peak_;
    }

    /// peak - most recent equity, >= 0. NaN before the first sample.
    [[nodiscard]] double current_drawdown() const noexcept {
        return count_ == 0 ? detail::drawdown_nan : peak_ - last_equity_;
    }

    /// Largest peak - equity over the whole history, >= 0. NaN before the first sample.
    [[nodiscard]] double max_drawdown() const noexcept {
        return count_ == 0 ? detail::drawdown_nan : max_abs_drawdown_;
    }

    /// (peak - equity) / peak at the current step. Empty while the peak is <= 0.
    [[nodiscard]] std::optional<double> current_relative_drawdown() const noexcept {
        if (count_ == 0 || peak_ <= 0.0) {
            return std::nullopt;
        }
        return (peak_ - last_equity_) / peak_;
    }

    /// Largest per-step relative drawdown. Empty while the peak is <= 0.
    [[nodiscard]] std::optional<double> max_relative_drawdown() const noexcept {
        if (count_ == 0 || peak_ <= 0.0) {
            return std::nullopt;
        }
        return max_rel_drawdown_;
    }

    /// Consecutive steps the equity has spent stricly below the peak.
    [[nodiscard]] std::int64_t current_underwater() const noexcept { return current_underwater_; }

    /// Longest such run over the whole history.
    [[nodiscard]] std::int64_t longest_underwater() const noexcept { return longest_underwater_; }

private:
    std::int64_t count_ = 0;
    double peak_ = -std::numeric_limits<double>::infinity();
    double last_equity_ = 0.0;
    double max_abs_drawdown_ = 0.0;
    double max_rel_drawdown_ = 0.0;
    std::int64_t current_underwater_ = 0;
    std::int64_t longest_underwater_ = 0;
};

} // namespace mms::math::stats

#endif // MMS_MATH_STATS_DRAWDOWN_HPP