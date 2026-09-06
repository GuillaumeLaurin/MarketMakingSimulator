#ifndef MMS_MATH_DIST_EXPONENTIAL_HPP
#define MMS_MATH_DIST_EXPONENTIAL_HPP

/// \file
/// Exponential sampling by inverse CDF: -log(U) / rate, with U from
/// open_unit_double so U is never 0 and the log is always finite. One engine
/// word per draw, no rejection.

#include <cmath>
#include <stdexcept>

#include <math/dist/normal.hpp>
#include <math/dist/primitives.hpp>

namespace mms::math::dist {

/// Exponential with the given rate (lambda). Mean is 1 / rate
class Exponential {
public:
    using result_type = double;
    static constexpr int words_per_sample = 1;

    explicit Exponential(double rate) : rate_(rate) {
        if (!std::isfinite(rate) || rate <= 0.0) {
            throw std::invalid_argument("Exponential: require finite rate > 0");
        }
    }

    template<Uint64Source G> [[nodiscard]] result_type operator()(G& g) const {
        return -std::log(open_unit_double(g)) / rate_;
    }

    [[nodiscard]] constexpr double rate() const noexcept { return rate_; }

    [[nodiscard]] constexpr double mean() const noexcept { return 1.0 / rate_; }

private:
    double rate_;
};

/// Standard gamma with shape >= 1 and unit scale, by Marsaglia-Tsang (2000)
/// rejection. This is the raw algorithm, isolated so it can be tested without
/// the scale / shape < 1 wrapping. Variable engine-word cost.
class StandardGamma {
public:
    using result_type = double;
    static constexpr int words_per_sample = -1;

    explicit StandardGamma(double shape) : shape_(shape) {
        if (!std::isfinite(shape) || shape < 1.0) {
            throw std::invalid_argument("StandardGamma: require finite shape >= 1");
        }
        d_ = shape - (1.0 / 3.0);
        c_ = 1.0 / std::sqrt(9.0 * d_);
    }

    template<Uint64Source G> [[nodiscard]] result_type operator()(G& g) const {
        const StandardNormal normal;

        while (true) {
            double x = normal(g);

            while ((1.0 + (c_ * x)) <= 0) {
                x = normal(g);
            }

            const double t = 1.0 + (c_ * x);
            const double v = t * t * t;

            const double u = open_unit_double(g);
            const double x2 = x * x;

            if (u < 1.0 - (0.0331 * x2 * x2)) {
                return d_ * v;
            }
            if (std::log(u) < (0.5 * x2) + (d_ * (1.0 - v + std::log(v)))) {
                return d_ * v;
            }
        }
    }

    [[nodiscard]] constexpr double shape() const noexcept { return shape_; }

private:
    double shape_;
    double d_ = 0.0;
    double c_ = 0.0;
};

/// Gamma(shape, scale). Marsaglia-Tsang (2000) rejection for shape >= 1; for
/// shape < 1, generate Gamma(shape + 1) and rescale by U^(1/shape). Consumes a
/// variable number of engine words (typically 2), so words_per_sample = -1 and
/// dist::discard() is a compile error for it.
class Gamma {
public:
    using result_type = double;
    static constexpr int words_per_sample = -1;

    Gamma(double shape, double scale)
        : shape_(shape), scale_(scale), core_(checked_core_shape(shape, scale)) {}

    template<Uint64Source G> [[nodiscard]] result_type operator()(G& g) const {
        double value = core_(g);
        if (shape_ < 1.0) {
            value *= std::pow(open_unit_double(g), 1.0 / shape_);
        }
        return value * scale_;
    }

    [[nodiscard]] constexpr double shape() const noexcept { return shape_; }

    [[nodiscard]] constexpr double scale() const noexcept { return scale_; }

private:
    static double checked_core_shape(double shape, double scale) {
        if (!std::isfinite(shape) || !std::isfinite(scale) || shape <= 0.0 || scale <= 0.0) {
            throw std::invalid_argument("Gamma: require finite shape > 0 and scale > 0");
        }
        return (shape < 1.0) ? (shape + 1.0) : shape;
    }

    double shape_;
    double scale_;
    StandardGamma core_;
};

template<Uint64Source G> [[nodiscard]] inline double exponential(G& g, double rate) {
    return Exponential{rate}(g);
}

template<Uint64Source G> [[nodiscard]] inline double gamma(G& g, double shape, double scale) {
    return Gamma{shape, scale}(g);
}

} // namespace mms::math::dist

#endif // MMS_MATH_DIST_EXPONENTIAL_HPP