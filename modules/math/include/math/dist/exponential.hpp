#ifndef MMS_MATH_DIST_EXPONENTIAL_HPP
#define MMS_MATH_DIST_EXPONENTIAL_HPP

/// \file
/// Exponential sampling by inverse CDF: -log(U) / rate, with U from
/// open_unit_double so U is never 0 and the log is always finite. One engine
/// word per draw, no rejection.

#include <cmath>
#include <stdexcept>

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

template<Uint64Source G> [[nodiscard]] inline double exponential(G& g, double rate) {
    return Exponential{rate}(g);
}

} // namespace mms::math::dist

#endif // MMS_MATH_DIST_EXPONENTIAL_HPP