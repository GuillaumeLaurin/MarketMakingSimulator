/// \file
/// Implementation of the batch statistics declared in <math/stats/batch.hpp>.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

#include <math/stats/batch.hpp>
#include <math/stats/quantile.hpp> // interpolated_quantile

namespace mms::math::stats {

namespace {

const double batch_nan = std::numeric_limits<double>::quiet_NaN();

double sum_squared_deviations(std::span<const double> xs, double m) noexcept {
    double m2 = 0.0;
    for (const double x : xs) {
        m2 += (x - m) * (x - m);
    }
    return m2;
}

} // namespace

double mean(std::span<const double> xs) noexcept {
    if (xs.empty()) {
        return batch_nan;
    }
    double s = 0.0;
    for (const double x : xs) {
        s += x;
    }
    return s / static_cast<double>(xs.size());
}

double population_variance(std::span<const double> xs) noexcept {
    if (xs.empty()) {
        return batch_nan;
    }
    return sum_squared_deviations(xs, mean(xs)) / static_cast<double>(xs.size());
}

double sample_variance(std::span<const double> xs) noexcept {
    if (xs.size() < 2) {
        return batch_nan;
    }
    return sum_squared_deviations(xs, mean(xs)) / static_cast<double>(xs.size() - 1);
}

double stddev(std::span<const double> xs) noexcept {
    return std::sqrt(sample_variance(xs));
}

double quantile(std::span<const double> xs, double p) {
    if (xs.empty()) {
        return batch_nan;
    }
    std::vector<double> sorted(xs.begin(), xs.end());
    std::ranges::sort(sorted.begin(), sorted.end());
    return interpolated_quantile(sorted, p);
}

std::vector<double> quantiles(std::span<const double> xs, std::span<const double> ps) {
    std::vector<double> out(ps.size(), batch_nan);
    if (xs.empty()) {
        return out;
    }
    std::vector<double> sorted(xs.begin(), xs.end());
    std::ranges::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < ps.size(); ++i) {
        out[i] = interpolated_quantile(sorted, ps[i]);
    }
    return out;
}

double max_drawdown(std::span<const double> equity) noexcept {
    if (equity.empty()) {
        return batch_nan;
    }
    double peak = equity[0];
    double worst = 0.0;
    for (const double e : equity) {
        peak = std::max(peak, e);
        worst = std::max(worst, peak - e);
    }
    return worst;
}

double sharpe_ratio(std::span<const double> returns, double risk_free) noexcept {
    const double sd = stddev(returns);
    if (!std::isfinite(sd) || sd == 0.0) {
        return batch_nan;
    }
    return (mean(returns) - risk_free) / sd;
}

double sortino_ratio(std::span<const double> returns, double risk_free) noexcept {
    if (returns.empty()) {
        return batch_nan;
    }
    double downside_sq = 0.0;
    for (const double r : returns) {
        const double below = std::min(r - risk_free, 0.0);
        downside_sq += below * below;
    }
    const double dd = std::sqrt(downside_sq / static_cast<double>(returns.size()));
    if (dd == 0.0) {
        return batch_nan;
    }
    return (mean(returns) - risk_free) / dd;
}

} // namespace mms::math::stats