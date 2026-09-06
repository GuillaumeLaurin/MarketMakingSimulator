#ifndef MMS_MATH_DIST_UNIFORM_HPP
#define MMS_MATH_DIST_UNIFORM_HPP

/// \file
/// Uniform samplers. UniformReal maps one canonical double affinely onto
/// [lo, hi). UniformInt uses Lemire's multiply-shift (no rejection) : exactly one
/// engine word, bias bounded by range / 2^64

#include <cmath>
#include <cstdint>
#include <stdexcept>

#include <math/bits.hpp>
#include <math/dist/primitives.hpp>

namespace mms::math::dist {

/// Uniform on [lo, hi). Degenerate lo == hi is allowed (always returns lo).
class UniformReal {
public:
    using result_type = double;
    static constexpr int words_per_sample = 1;

    UniformReal(double lo, double hi) : lo_(lo), span_(hi - lo) {
        if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi || !std::isfinite(span_)) {
            throw std::invalid_argument("UniformReal: require finite lo <= hi with finite hi - lo");
        }
    }

    template<Uint64Source G> [[nodiscard]] result_type operator()(G& g) const {
        return lo_ + (span_ * canonical_double(g));
    }

    [[nodiscard]] constexpr double lo() const noexcept { return lo_; }

    [[nodiscard]] constexpr double hi() const noexcept { return lo_ + span_; }

private:
    double lo_;
    double span_;
};

/// Uniform on the inclusive integer range [lo, hi]. Lemire multiply-shift,
/// no rejection step.
class UniformInt {
public:
    using result_type = std::int64_t;
    static constexpr int words_per_sample = 1;

    constexpr UniformInt(std::int64_t lo, std::int64_t hi) : lo_(lo), hi_(hi) {
        if (lo > hi) {
            throw std::invalid_argument("UniformInt: require lo <= hi");
        }
    }

    template<Uint64Source G> [[nodiscard]] constexpr result_type operator()(G& g) const {
        // Outcome count. lo == INT64_MIN && hi == INT64_MAX wraps to 0, read as
        // "the whole 2^64 span".
        const std::uint64_t range =
            static_cast<std::uint64_t>(hi_) - static_cast<std::uint64_t>(lo_) + 1ULL;
        const std::uint64_t x = g();
        const std::uint64_t offset = (range == 0ULL) ? x : mulhi64(x, range);
        return static_cast<result_type>(static_cast<std::uint64_t>(lo_) + offset);
    }

    [[nodiscard]] constexpr std::int64_t lo() const noexcept { return lo_; }

    [[nodiscard]] constexpr std::int64_t hi() const noexcept { return hi_; }

private:
    std::int64_t lo_;
    std::int64_t hi_;
};

/// One-off convenience draws. Build a sampler for anything in a loop
template<Uint64Source G> [[nodiscard]] inline double uniform_real(G& g, double lo, double hi) {
    return UniformReal{lo, hi}(g);
}

template<Uint64Source G>
[[nodiscard]] inline std::int64_t uniform_int(G& g, std::int64_t lo, std::int64_t hi) {
    return UniformInt{lo, hi}(g);
}

} // namespace mms::math::dist

#endif // MMS_MATH_DIST_UNIFORM_HPP