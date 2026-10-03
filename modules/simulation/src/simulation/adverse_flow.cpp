/// \file
/// Implementation of adverse flow (<simulation/adverse_flow.hpp>).

#include <simulation/adverse_flow.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <math/dist/discrete.hpp>
#include <math/dist/exponential.hpp>
#include <math/dist/uniform.hpp>
#include "mm/types.hpp"
#include "simulation/size_spec.hpp"

namespace mms::simulation {

namespace {

/// math's Poisson sampler only accepts a mean in (0, 30]; a larger mean is
/// drawn as several independent chunks, whose sum is Poisson again.
constexpr double max_poisson_mean = 30.0;

/// A mean this large per tick is a configuration mistake, and would make the
/// chunked draw (and the order vector) enormous rather than fail.
constexpr double max_arrivals_per_tick = 1.0e6;

/// Largest exponential size handed to the integer conversion: far above any
/// realistic trade, far below the point where the conversion overflows.
constexpr double max_exponential_size = 9.0e15;

[[noreturn]] void reject(const char* message) {
    throw std::invalid_argument(std::string("adverse_flow: ") + message);
}

void require_correlation(double correlation) {
    if (!std::isfinite(correlation) || correlation < 0.0 || correlation > 1.0) {
        reject("direction correlation must be finite and in [0, 1]");
    }
}

void require_last_move(double last_move) {
    if (!std::isfinite(last_move)) {
        reject("last price move must be finite");
    }
}

void require_size_spec(const DistributionSpec& spec) {
    std::visit(
        [](const auto& concrete) {
            using Spec = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<Spec, UniformIntSpec>) {
                if (concrete.min < 1 || concrete.min > concrete.max) {
                    reject("a uniform size needs 1 <= min <= max");
                }
            } else {
                static_assert(std::is_same_v<Spec, ExponentialSpec>);
                if (!std::isfinite(concrete.mean) || concrete.mean <= 0.0) {
                    reject("an exponential size needs a finite mean > 0");
                }
            }
        },
        spec
    );
}

} // namespace

std::int64_t draw_arrival_count(double lambda, double dt, math::rng::RngEngine &engine) {
    if (!std::isfinite(lambda) || lambda < 0.0) {
        reject("lambda must be finite and >= 0");
    }

    if (!std::isfinite(dt) || dt <= 0.0) {
        reject("dt must be finite and > 0");
    }
    const double mean = lambda * dt;
    if (!(mean <= max_arrivals_per_tick)) {
        reject("lambda * dt (arrivals per tick) must be at most 1e6");
    }

    std::int64_t count = 0;
    double remaining = mean;
    while (remaining > 0.0) {
        const double chunk = std::min(remaining, max_poisson_mean);
        count += math::dist::poisson(engine, chunk);
        remaining -= chunk;
    }
    return count;
}

mm::Side draw_direction(double correlation, double last_move, math::rng::RngEngine &engine) {
    require_correlation(correlation);
    require_last_move(last_move);

    // Both coins are always drawn, so the stream never depends on the outcome.
    const bool follows_the_move = math::dist::bernoulli(engine, correlation);
    const bool fair_coin_buys = math::dist::bernoulli(engine, 0.5);

    if (follows_the_move && last_move != 0.0) {
        return last_move > 0.0 ? mm::Side::Buy : mm::Side::Sell;
    }
    return fair_coin_buys ? mm::Side::Buy : mm::Side::Sell;
}

mm::Quantity draw_size(const DistributionSpec &spec, math::rng::RngEngine &engine) {
    require_size_spec(spec);

    return std::visit(
        [&engine](const auto& concrete) -> mm::Quantity {
            using Spec = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<Spec, UniformIntSpec>) {
                return math::dist::uniform_int(engine, concrete.min, concrete.max);
            } else {
                static_assert(std::is_same_v<Spec, ExponentialSpec>);
                const double raw = math::dist::exponential(engine, 1.0 / concrete.mean);
                // Nearest whole share, never below 1, never past the clamp.
                return static_cast<mm::Quantity>(
                    std::clamp(std::round(raw), 1.0, max_exponential_size)
                );
            }
        },
        spec
    );
}

std::vector<mm::MarketOrder> generate_adverse_flow(const AssetSpec &asset, 
                                                   double last_move, 
                                                   double dt, 
                                                   math::rng::RngEngine &engine) {
    // Everything is validated before the first draw, so a rejected call
    // leaves the engine exactly where it was.
    require_correlation(asset.adverse_flow_direction_correlation);
    require_last_move(last_move);
    require_size_spec(asset.adverse_flow_size);

    const std::int64_t count = draw_arrival_count(asset.adverse_flow_lambda, dt, engine);

    std::vector<mm::MarketOrder> orders;
    orders.reserve(static_cast<std::size_t>(count));
    for (std::int64_t i = 0; i < count; ++i) {
        const mm::Side side =
            draw_direction(asset.adverse_flow_direction_correlation, last_move, engine);
        const mm::Quantity quantity = draw_size(asset.adverse_flow_size, engine);
        orders.push_back(mm::MarketOrder{
            .side = side,
            .quantity = quantity,
            .time_in_force = mm::MarketTimeInForce::IOC
        });
    }
    return orders;
}

} // namespace mms::simulation