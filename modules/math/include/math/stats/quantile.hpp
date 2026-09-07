#ifndef MMS_MATH_STATS_QUANTILE_HPP
#define MMS_MATH_STATS_QUANTILE_HPP

/// \file
/// PSquareQuantile: single-quantile estimator by the P^2 algorithm (Jain &
/// Chandrasekaran, 1985). O(1) memory (5 markers), O(1) per update. Approximate:
/// poor for heavy tails, extreme quantiles, or few samples. For an exact
/// quantile over a stored series use the batch quantile() instead.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>

namespace mms::math::stats {

namespace detail {
inline constexpr double quantil_nan = std::numeric_limits<double>::quiet_NaN();
} // namespace detail

/// Type-7 quantile of an ASCENDING span. NaN for an empty span or p outside
/// [0, 1] / non-finite
[[nodiscard]] inline double interpolated_quantile(std::span<const double> sorted,
                                                  double p) noexcept {
    if (sorted.empty() || !std::isfinite(p) || p < 0.0 || p > 1.0) {
        return detail::quantil_nan;
    }
    const std::size_t m = sorted.size();
    if (m == 1) {
        return sorted[0];
    }
    const double h = static_cast<double>(m - 1) * p;
    const auto lo = static_cast<std::size_t>(std::floor(h));
    if (lo + 1 >= m) {
        return sorted[m - 1];
    }
    return sorted[lo] + ((h - static_cast<double>(lo)) * (sorted[lo + 1] - sorted[lo]));
}

/// The five-marker state of the P^2 algorithm. Narrow contract: from_sorted()
/// expects ascending heights and 0 < p < 1; observe() expects a finite x.
class P2State {
public:
    P2State() = default;

    [[nodiscard]] static P2State from_sorted(double p,
                                             std::array<double, 5> sorted_heights) noexcept {
        P2State s;
        s.q_ = sorted_heights;
        s.n_ = {1.0, 2.0, 3.0, 4.0, 5.0};
        s.np_ = {1.0, 1.0 + (2.0 * p), 1.0 + (4.0 * p), 3.0 + (2.0 * p), 5.0};
        s.dn_ = {0.0, p / 2.0, p, (1.0 + p) / 2.0, 1.0};
        return s;
    }

    /// Parabolic prediction for interior marker i moving by d  (+/-1). Pure.
    [[nodiscard]] double parabolic(std::size_t i, double d) const noexcept {
        const double nm1 = n_[i - 1];
        const double ni = n_[i];
        const double np1 = n_[i + 1];
        return q_[i] +
               ((d / (np1 - nm1)) * ((((ni - nm1 + d) * (q_[i + 1] - q_[i])) / (np1 - ni)) +
                                     ((((np1 - ni - d)) * (q_[i] - q_[i - 1])) / (ni - nm1))));
    }

    /// Linear prediction for interior marker i moving by d (+/-1). Pure.
    [[nodiscard]] double linear(std::size_t i, double d) const noexcept {
        const std::size_t j = (d > 0.0) ? (i + 1) : (i - 1);
        return q_[i] + ((d * (q_[j] - q_[i])) / (n_[j] - n_[i]));
    }

    /// One P^2 step: locate the cell, bump positions and desired positions,
    /// then adjust the three interior markers.
    void observe(double x) noexcept {
        std::size_t k = 0;
        if (x < q_[0]) {
            q_[0] = x;
        } else if (x >= q_[4]) {
            q_[4] = x;
            k = 3;
        } else {
            while (k < 3 && x >= q_[k + 1]) {
                ++k;
            }
        }

        for (std::size_t i = k + 1; i < 5; ++i) {
            n_[i] += 1.0;
        }
        for (std::size_t i = 0; i < 5; ++i) {
            np_[i] += dn_[i];
        }

        for (std::size_t i = 1; i <= 3; ++i) {
            const double d = np_[i] - n_[i];
            if ((d >= 1.0 && (n_[i + 1] - n_[i]) > 1.0) ||
                (d <= -1.0 && (n_[i - 1] - n_[i]) < -1.0)) {
                const double dir = (d >= 0.0) ? 1.0 : -1.0;
                const double qp = parabolic(i, dir);
                q_[i] = (q_[i - 1] < qp && qp < q_[i + 1]) ? qp : linear(i, dir);
                n_[i] += dir;
            }
        }
    }

    [[nodiscard]] double estimate() const noexcept { return q_[2]; }

    [[nodiscard]] const std::array<double, 5>& heights() const noexcept { return q_; }

    [[nodiscard]] const std::array<double, 5>& positions() const noexcept { return n_; }

    [[nodiscard]] const std::array<double, 5>& desired_positions() const noexcept { return np_; }

private:
    std::array<double, 5> q_{};
    std::array<double, 5> n_{};
    std::array<double, 5> np_{};
    std::array<double, 5> dn_{};
};

class PSquareQuantile {
public:
    explicit PSquareQuantile(double p) : p_(p) {
        if (!std::isfinite(p) || p <= 0.0 || p >= 1.0) {
            throw std::invalid_argument("PSquareQuantile: require 0 < p < 1");
        }
    }

    /// Fold one observation in. A non-finite x is ignored (no-op).
    void add(double x) noexcept {
        if (!std::isfinite(x)) {
            return;
        }
        if (!state_.has_value()) {
            warmup_[static_cast<std::size_t>(count_)] = x;
            ++count_;
            if (count_ == 5) {
                std::array<double, 5> sorted = warmup_;
                insertion_sort(sorted, 5);
                state_ = P2State::from_sorted(p_, sorted);
            }
            return;
        }
        ++count_;
        state_->observe(x);
    }

    [[nodiscard]] std::int64_t count() const noexcept { return count_; }

    /// The target quantile in (0, 1)
    [[nodiscard]] double quantile() const noexcept { return p_; }

    [[nodiscard]] double value() const noexcept {
        if (count_ == 0) {
            return detail::quantil_nan;
        }
        if (state_.has_value()) {
            return state_->estimate();
        }
        std::array<double, 5> sorted = warmup_;
        const auto m = static_cast<std::size_t>(count_);
        insertion_sort(sorted, m);
        return interpolated_quantile(std::span<const double>{sorted.data(), m}, p_);
    }

private:
    static void insertion_sort(std::array<double, 5>& a, std::size_t m) noexcept {
        for (std::size_t i = 1; i < m; ++i) {
            const double key = a[i];
            std::size_t j = i;
            while (j > 0 && a[j - 1] > key) {
                a[j] = a[j - 1];
                --j;
            }
            a[j] = key;
        }
    }

    double p_;
    std::int64_t count_ = 0;
    std::array<double, 5> warmup_{};
    std::optional<P2State> state_;
};

} // namespace mms::math::stats

#endif // MMS_MATH_STATS_QUANTILE_HPP