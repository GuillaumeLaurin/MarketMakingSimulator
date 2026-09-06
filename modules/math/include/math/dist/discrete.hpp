#ifndef MMS_MATH_DIST_DISCRETE_HPP
#define MMS_MATH_DIST_DISCRETE_HPP

/// \file
/// Discrete samplers. Bernoulli is one canonical double against a threshold.
/// Poisson is CDF inversion: one uniform, walk up the pmf. Both consume exactly
/// one engine word regardless of their parameter, so a parameter sweep never
/// desyncs the stream.

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <math/dist/primitives.hpp>

namespace mms::math::dist {

/// Bernoulli(p): true with probability p. Never short-circuits p == 0 or p == 1m
/// so it always draws exactly one word.
class Bernoulli {
public:
    using result_type = bool;
    static constexpr int words_per_sample = 1;

    explicit Bernoulli(double p) : p_(p) {
        if (!std::isfinite(p) || p < 0.0 || p > 1.0) {
            throw std::invalid_argument("Bernoulli: require 0 <= p <= 1");
        }
    }

    template<Uint64Source G> [[nodiscard]] result_type operator()(G& g) const {
        return canonical_double(g) < p_;
    }

    [[nodiscard]] constexpr double p() const noexcept { return p_; }

private:
    double p_;
};

/// Poisson(mean) by CDF inversion. One uniform per draw; the loop count depends
/// only on the value produced, not on the engine. Precondition 0 < mean <= 30
/// (past that exp(-mean)) undeflows and the walk gets long).
class Poisson {
public:
    using result_type = std::int64_t;
    static constexpr int words_per_sample = 1;

    explicit Poisson(double mean) : mean_(mean), p0_(std::exp(-mean)) {
        if (!std::isfinite(mean) || mean <= 0.0 || mean > 30.0) {
            throw std::invalid_argument("Poisson: require finite 0 < mean <= 30");
        }
    }

    template<Uint64Source G> [[nodiscard]] result_type operator()(G& g) const {
        const double u = canonical_double(g);
        double cdf = p0_;
        double term = p0_;
        std::int64_t k = 0;
        while (u > cdf && k < kmax) {
            ++k;
            term *= mean_ / static_cast<double>(k);
            cdf += term;
        }
        return k;
    }

    [[nodiscard]] constexpr double mean() const noexcept { return mean_; }

private:
    // Loop guard: only reached if rounding keeps cdf just below u. With
    // mean <= 30 a real draw never approches this.
    static constexpr std::int64_t kmax = 512;

    double mean_;
    double p0_;
};

/// Geometric(p): number of failures before first success, support
/// {0, 1, 2, ...}, P(k) = (1 - p)^k * p. Inverse CDF, one word, no rejection.
/// A very small p can produce a very large k; the result sturates at
/// INT64_MAX rather than overflowing.
class Geometric {
public:
    using result_type = std::int64_t;
    static constexpr int words_per_sample = 1;

    explicit Geometric(double p) : log_q_(std::log1p(-p)), p_(p) {
        if (!std::isfinite(p) || p <= 0.0 || p > 1.0) {
            throw std::invalid_argument("Geometric: require 0 < p <= 1");
        }
    }

    template<Uint64Source G> [[nodiscard]] result_type operator()(G& g) const {
        // p == 1 -> log_q_ == -inf -> x == 0. Otherwise x >= 0
        const double x = std::floor(std::log(open_unit_double(g)) / log_q_);
        if (x >= 9223372036854774784.0) {
            return std::numeric_limits<std::int64_t>::max();
        }
        return static_cast<result_type>(x);
    }

    [[nodiscard]] constexpr double p() const noexcept { return p_; }

private:
    double log_q_;
    double p_;
};

template<Uint64Source G> [[nodiscard]] inline bool bernoulli(G& g, double p) {
    return Bernoulli{p}(g);
}

template<Uint64Source G> [[nodiscard]] inline std::int64_t poisson(G& g, double mean) {
    return Poisson{mean}(g);
}

template<Uint64Source G> [[nodiscard]] inline std::int64_t geometric(G& g, double p) {
    return Geometric{p}(g);
}

} // namespace mms::math::dist

#endif // MMS_MATH_DIST_DISCRETE_HPP