#ifndef MMS_MATH_DIST_NORMAL_HPP
#define MMS_MATH_DIST_NORMAL_HPP

/// \file
/// Normal sampling by inverse CDF. probit() is Wichura's AS241 rationnal
/// approximation of the standard-normal quantile (Applied Statistics 37, 1988)
/// the algorithm R's qnorm uses, accurate to ~1e-16 over the representable range.
/// StandardNormal runs one open-unit double through probit; Normal is the affine
/// wrapper mu + sigma * z. One engine word per draw, no rejection, no pairing,
/// no hidden state.

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include <math/dist/primitives.hpp>

namespace mms::math::dist {

namespace detail {

/// Horner evaluation, highest-degree coefficient first.
template <std::size_t N>
[[nodiscard]] constexpr double horner(const std::array<double, N>& c, double x) noexcept {
    double acc = c[0];
    for (std::size_t i = 1; i < N; ++i) {
        acc = (acc * x) + c[i];
    }
    return acc;
}

// AS241 coefficient sets. a/b: central region, argument r = 0.180625 - q*q.
inline constexpr std::array<double, 8> as241_a{
    2.5090809287301226727e+3, 3.3430575583588128105e+4, 6.7265770927008700853e+4,
    4.5921953931549871457e+4, 1.3731693765509461125e+4, 1.9715909503065514427e+3,
    1.3314166789178437745e+2, 3.3871328727963666080e+0};
inline constexpr std::array<double, 8> as241_b{
    5.2264952788528545610e+3, 2.8729085735721942674e+4, 3.9307895800092710610e+4,
    2.1213794301586595867e+4, 5.3941960214247511077e+3, 6.8718700749205790830e+2,
    4.2313330701600911252e+1, 1.0};

// c/d: near tail, r = sqrt(-log(min(p, 1 - p))) in (~1.9, 5], argument r - 1.6.
inline constexpr std::array<double, 8> as241_c{
    7.74545014278341407640e-4, 2.27238449892691845833e-2, 2.41780725177450611770e-1,
    1.27045825245236838258e+0, 3.64784832476320460504e+0, 5.76949722146069140550e+0,
    4.63033784615654529590e+0, 1.42343711074968357734e+0};
inline constexpr std::array<double, 8> as241_d{
    1.05075007164441684324e-9, 5.47593808499534494600e-4, 1.51986665636164571966e-2,
    1.48103976427480074590e-1, 6.89767334985100004550e-1, 1.67638483018380384940e+0,
    2.05319162663775882187e+0, 1.0};

// e/f: far tail, r > 5, argument r - 5.
inline constexpr std::array<double, 8> as241_e{
    2.01033439929228813265e-7, 2.71155556874348757815e-5, 1.24266094738807843860e-3,
    2.65321895265761230930e-2, 2.96560571828504891230e-1, 1.78482653991729133580e+0,
    5.46378491116411436990e+0, 6.65790464350110377720e+0};
inline constexpr std::array<double, 8> as241_f{
    2.04426310338993978564e-15, 1.42151175831644588870e-7, 1.84631831751005468180e-5,
    7.86869131145613259100e-4, 1.48753612908506148525e-2, 1.36929880922735805310e-1,
    5.99832206555887937690e-1, 1.0};

} // namespace detail

/// Standard-normal quantile: the x with Phi(x) == p. Narrow contract: p must be
/// in (0, 1); outside, returns NaN and never throws, so a caller that always
/// feeds open_unit_double stays branch-free. probit(0.5) == 0 exactly; probit is
/// odd up to the rounding of 1 - p.
[[nodiscard]] inline double probit(double p) noexcept {
    if (!(p > 0.0 && p < 1.0)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double q = p - 0.5;

    if (q >= -0.425 && q <= 0.425) {
        const double r = 0.180625 - (q * q);
        return q * detail::horner(detail::as241_a, r) / detail::horner(detail::as241_b, r);
    }

    double r = std::sqrt(-std::log((q < 0.0) ? p : (1.0 - p)));

    double value = 0.0;

    if (r <= 5.0) {
        r -= 1.6;
        value = detail::horner(detail::as241_c, r) / detail::horner(detail::as241_d, r);
    } else {
        r -= 5;
        value = detail::horner(detail::as241_e, r) / detail::horner(detail::as241_f, r);
    }

    return (q < 0.0) ? -value : value;
}

/// N(0, 1) by probit(open_unit_double(g)).
struct StandardNormal {
    using result_type = double;
    static constexpr int words_per_sample = 1;

    template <Uint64Source G>
    [[nodiscard]] result_type operator()(G& g) const {
        return probit(open_unit_double(g));
    }
};

/// N(mu, sigma^2). Composes StandardNormal: mu + sigma * z;
class Normal {
public:
    using result_type = double;
    static constexpr int words_per_sample = 1;

    Normal(double mu, double sigma) : mu_(mu), sigma_(sigma) {
        if (!std::isfinite(mu) || !std::isfinite(sigma) || sigma <= 0.0) {
            throw std::invalid_argument("Normal: require finite mu and sigma > 0");
        }
    }

    template <Uint64Source G>
    [[nodiscard]] result_type operator()(G& g) const {
        return mu_ + (sigma_ * StandardNormal{}(g));
    }

    [[nodiscard]] constexpr double mean() const noexcept { return mu_; }
    [[nodiscard]] constexpr double stddev() const noexcept { return sigma_; }

private:
    double mu_;
    double sigma_;
};

template <Uint64Source G>
[[nodiscard]] inline double standard_normal(G& g) {
    return StandardNormal{}(g);
}

template <Uint64Source G>
[[nodiscard]] inline double normal(G& g, double mu, double sigma) {
    return Normal{mu, sigma}(g);
}

} // namespace mms::math::dist

#endif // MMS_MATH_DIST_NORMAL_HPP