#ifndef MMS_MM_HORIZON_HPP
#define MMS_MM_HORIZON_HPP

/// \file
/// The time horizon tau that Avellaneda-Stoikov's formulas take as a plain
/// double (math never decides what tau means: that is a strategy choice, and
/// it lives here). Two policies: a constant, rolling horizon, and a finite one
/// that shrinks as the end of the session approaches. Times and tau are in the
/// simulation's time unit, the same one as the per-unit-time variance sigma^2.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>

namespace mms::mm {

/// The same tau at every instant: the market maker always looks a fixed
/// distance ahead. Must be finite and > 0.
struct ConstantHorizon {
    double tau;
};

/// tau = end_time - time, so the horizon shrinks to nothing at the end of
/// the session. `min_tau` floors it: past end_time (or closer to it than
/// min_tau) tau stays at min_tau, never negative. end_time must be finite,
/// min_tau finite and >= 0.]
struct FiniteHorizon {
    double end_time;
    double min_tau = 0.0;
};

using HorizonPolicy = std::variant<ConstantHorizon, FiniteHorizon>;

namespace detail {

inline void require_finite_horizon_value(double value, const char* what) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string("horizon: ") + what + " must be finite");
    }
}

struct TauAt {
    double time;

    double operator()(const ConstantHorizon& horizon) const {
        require_finite_horizon_value(horizon.tau, "tau");
        if (horizon.tau <= 0.0) {
            throw std::invalid_argument("horizon: tau must be > 0");
        }
        return horizon.tau;
    }

    double operator()(const FiniteHorizon& horizon) const {
        require_finite_horizon_value(horizon.end_time, "end_time");
        require_finite_horizon_value(horizon.min_tau, "min_tau");
        if (horizon.min_tau < 0.0) {
            throw std::invalid_argument("horizon: min_tau must be >= 0");
        }
        return std::max(horizon.end_time - time, horizon.min_tau);
    }
};

} // namespace detail

/// The tau to use at simulation time `time`. Throws std::invalid_argument for
/// a non-finite time or an invalid policy. A default-constructed
/// HorizonPolicy holds ConstantHorizon{0} and is rejected here, on purpose.
[[nodiscard]] inline double tau_at(const HorizonPolicy& policy, double time) {
    detail::require_finite_horizon_value(time, "time");
    return std::visit(detail::TauAt{.time = time}, policy);
}

} // namespace mms::mm

#endif // MMS_MM_HORIZON_HPP