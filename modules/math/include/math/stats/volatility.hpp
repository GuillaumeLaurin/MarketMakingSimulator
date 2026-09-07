#ifndef MMS_MATH_STATS_VOLATILITY_HPP
#define MMS_MATH_STATS_VOLATILITY_HPP

/// \file
/// Scalar per-asset volatility estimators and their type-erased wrapper.
/// ConstantVolEstimator is the default (ignores observations, returns a config
/// sigma). EwmaVolEstimator wraps EwmaEstimator. RollingVolEstimator is a
/// bounded ring buffer recomputed lazily (O(window) only after a change).
/// AnyVolatilityEstimator carries any of them at the mms config seam.

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <math/stats/ewma.hpp>
#include <math/stats/traits.hpp>

namespace mms::math::stats {

/// Ignores observations; sigma() is the constant supplied at constrcution. The
/// default estimator.
class ConstantVolEstimator {
public:
    explicit ConstantVolEstimator(double sigma) : sigma_(sigma) {
        if (!std::isfinite(sigma) || sigma <= 0.0) {
            throw std::invalid_argument("ConstantVolEstimator: require finite sigma > 0");
        }
    }

    void update(double x) noexcept {
        if (std::isfinite(x)) {
            ++count_;
        }
    }

    [[nodiscard]] double sigma() const noexcept { return sigma_; }

    [[nodiscard]] std::int64_t sample_count() const noexcept { return count_; }

private:
    double sigma_;
    std::int64_t count_ = 0;
};

/// EWMA standard deviation. NaN before the first sample.
class EwmaVolEstimator {
public:
    explicit EwmaVolEstimator(double half_life) : ewma_(half_life) {}

    void update(double x) noexcept { ewma_.add(x); }

    [[nodiscard]] double sigma() const noexcept { return ewma_.stddev(); }

    [[nodiscard]] std::int64_t sample_count() const noexcept { return ewma_.sample_count(); }

    [[nodiscard]] double half_life() const noexcept { return ewma_.half_life(); }

private:
    EwmaEstimator ewma_;
};

/// Sample standard deviation over the last `window` finite observations. O(1)
/// update; sigma() recomputes in O(window) only when the window has changed
/// since the last call, then memoizes.
class RollingVolEstimator {
public:
    explicit RollingVolEstimator(std::size_t window) : buf_(window) {
        if (window < 2) {
            throw std::invalid_argument("RollingVolEstimator: require window >= 2");
        }
    }

    void update(double x) noexcept {
        if (!std::isfinite(x)) {
            return;
        }
        buf_[head_] = x;
        head_ = (head_ + 1) % buf_.size();
        filled_ = std::min(filled_ + 1, buf_.size());
        ++count_;
        dirty_ = true;
    }

    [[nodiscard]] double sigma() const noexcept {
        if (dirty_) {
            cached_sigma_ = recompute();
            dirty_ = false;
        }
        return cached_sigma_;
    }

    [[nodiscard]] std::int64_t sample_count() const noexcept { return count_; }

    [[nodiscard]] std::size_t window() const noexcept { return buf_.size(); }

private:
    [[nodiscard]] double recompute() const noexcept {
        if (filled_ < 2) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        double sum = 0.0;
        for (std::size_t i = 0; i < filled_; ++i) {
            sum += buf_[i];
        }
        const double mean = sum / static_cast<double>(filled_);
        double m2 = 0.0;
        for (std::size_t i = 0; i < filled_; ++i) {
            const double d = buf_[i] - mean;
            m2 += d * d;
        }
        return std::sqrt(m2 / static_cast<double>(filled_ - 1));
    }

    std::vector<double> buf_;
    std::size_t head_ = 0;
    std::size_t filled_ = 0;
    std::int64_t count_ = 0;
    mutable bool dirty_ = true;
    mutable double cached_sigma_ = std::numeric_limits<double>::quiet_NaN();
};

/// Type-erased carrier for any VolatilityEstimator, for the mms config seam.
/// Small-buffer storage (no heap per call beyond the estimator's own).
class AnyVolatilityEstimator {
public:
    static constexpr std::size_t buffer_capacity = 96;

    template<class E>
        requires(VolatilityEstimator<E> && !std::same_as<E, AnyVolatilityEstimator>)
    explicit AnyVolatilityEstimator(E estimator) : vtable_(&vtable_for<E>()) {
        static_assert(sizeof(E) <= buffer_capacity, "estimator exceeds buffer_capacity");
        static_assert(alignof(E) <= alignof(std::max_align_t), "estimator over-aligned");
        static_assert(std::is_nothrow_move_constructible_v<E>, "estimator must be nothrow-movable");
        ::new (raw()) E(std::move(estimator));
    }

    AnyVolatilityEstimator(const AnyVolatilityEstimator& other) : vtable_(other.vtable_) {
        vtable_->copy(other.raw(), raw());
    }

    AnyVolatilityEstimator(AnyVolatilityEstimator&& other) noexcept : vtable_(other.vtable_) {
        vtable_->move(other.raw(), raw());
    }

    AnyVolatilityEstimator& operator=(const AnyVolatilityEstimator& other) {
        if (this != &other) {
            vtable_->destroy(raw());
            vtable_ = other.vtable_;
            vtable_->copy(other.raw(), raw());
        }
        return *this;
    }

    AnyVolatilityEstimator& operator=(AnyVolatilityEstimator&& other) noexcept {
        if (this != &other) {
            vtable_->destroy(raw());
            vtable_ = other.vtable_;
            vtable_->move(other.raw(), raw());
        }
        return *this;
    }

    ~AnyVolatilityEstimator() { vtable_->destroy(raw()); }

    void update(double x) { vtable_->update(raw(), x); }

    [[nodiscard]] double sigma() const { return vtable_->sigma(raw()); }

    [[nodiscard]] std::int64_t sample_count() const { return vtable_->sample_count(raw()); }

private:
    [[nodiscard]] void* raw() noexcept { return storage_.data(); }

    [[nodiscard]] const void* raw() const noexcept { return storage_.data(); }

    struct VTable {
        void (*update)(void*, double);
        double (*sigma)(const void*);
        std::int64_t (*sample_count)(const void*);
        void (*copy)(const void*, void*);
        void (*move)(void*, void*) noexcept;
        void (*destroy)(void*) noexcept;
    };

    template<class E> static const VTable& vtable_for() noexcept {
        static const VTable table{
            [](void* p, double x) { static_cast<E*>(p)->update(x); },
            [](const void* p) -> double { return static_cast<const E*>(p)->sigma(); },
            [](const void* p) -> std::int64_t { return static_cast<const E*>(p)->sample_count(); },
            [](const void* src, void* dst) { ::new (dst) E(*static_cast<const E*>(src)); },
            [](void* src, void* dst) noexcept { ::new (dst) E(std::move(*static_cast<E*>(src))); },
            [](void* p) noexcept { std::destroy_at(static_cast<E*>(p)); },
        };
        return table;
    }

    alignas(std::max_align_t) std::array<std::byte, buffer_capacity> storage_{};
    const VTable* vtable_;
};

} // namespace mms::math::stats

#endif // MMS_MATH_STATS_VOLATILITY_HPP