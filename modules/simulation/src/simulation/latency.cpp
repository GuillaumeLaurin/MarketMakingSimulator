/// \file
/// Implementation of the delay draws

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include <math/dist/exponential.hpp>
#include <math/dist/uniform.hpp>
#include <simulation/latency.hpp>

#include "math/rng/engine.hpp"
#include "simulation/delay_spec.hpp"

namespace mms::simulation {

namespace {

[[noreturn]] void reject(const char* message) {
    throw std::invalid_argument(std::string("latency: ") + message);
}

} // namespace

void validate_delay_spec(const DelaySpec& spec) {
    std::visit(
        [](const auto& concrete) {
            using Spec = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<Spec, ConstantDelaySpec>) {
                if (!std::isfinite(concrete.delay) || concrete.delay < 0.0) {
                    reject("a constant delay must be finite and >= 0");
                }
            } else if constexpr (std::is_same_v<Spec, UniformDelaySpec>) {
                if (!std::isfinite(concrete.min) || !std::isfinite(concrete.max) ||
                    concrete.min < 0.0 || concrete.min > concrete.max) {
                    reject("a uniform delay needs finite 0 <= min <= max");
                }
            } else {
                static_assert(std::is_same_v<Spec, ExponentialDelaySpec>);
                if (!std::isfinite(concrete.mean) || concrete.mean <= 0.0) {
                    reject("an exponential delay needs a finite mean > 0");
                }
            }
        },
        spec);
}

double draw_delay(const DelaySpec& spec, math::rng::RngEngine& engine) {
    validate_delay_spec(spec);

    return std::visit(
        [&engine](const auto& concrete) -> double {
            using Spec = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<Spec, ConstantDelaySpec>) {
                return concrete.delay;
            } else if constexpr (std::is_same_v<Spec, UniformDelaySpec>) {
                return math::dist::uniform_real(engine, concrete.min, concrete.max);
            } else {
                static_assert(std::is_same_v<Spec, ExponentialDelaySpec>);
                const double raw = math::dist::exponential(engine, 1.0 / concrete.mean);
                // A huge mean must never schedule a request at +infinity.
                return std::min(raw, std::numeric_limits<double>::max());
            }
        },
        spec);
}

} // namespace mms::simulation