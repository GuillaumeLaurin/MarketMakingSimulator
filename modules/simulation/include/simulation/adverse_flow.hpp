#ifndef MMS_SIMULATION_ADVERSE_FLOW_HPP
#define MMS_SIMULATION_ADVERSE_FLOW_HPP

/// \file
/// Adverse order flow: what hits the market maker's quotes on one asset
/// during one tick. Pure functions of their inputs (the last price move is
/// passed in, not remembered), so each can be tested with nothing but an
/// RngEngine. The flow is always a MarketOrder with IOC: it only ever takes
/// liquidity, it never rests.
///
/// Draw order is fixed, which is what makes a run reproducible: first the
/// arrival count, then for each occurrence its direction (always two
/// engine words, even when the outcome does not need both) and its size.

#include <cstdint>
#include <vector>

#include <mm/types.hpp>

#include <math/rng/engine.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/simulation_export.hpp>
#include <simulation/size_spec.hpp>

namespace mms::simulation {

/// Number of occurrences in one tick: Poisson with mean lambda * dt.
/// Returns 0 without drawing when lambda is 0. Throws std::invalid_argument
/// unless lambda is finite and >= 0 and dt is finite and > 0.
[[nodiscard]] MMS_SIMULATION_EXPORT std::int64_t
draw_arrival_count(double lambda, double dt, math::rng::RngEngine& engine);

/// Direction of one occurrence. With probability `correlation` it follows
/// the sign of `last_move` (up means buy, down means sell), otherwise it is
/// a fair coin; a `last_move` of exactly 0 has no direction to follow and
/// also falls back to the coin. So after an up move, P(Buy) is
/// (1 + correlation) / 2. Throws std::invalid_argument unless correlation
/// is finite and in [0, 1] and last_move is finite.
[[nodiscard]] MMS_SIMULATION_EXPORT mm::Side
draw_direction(double correlation, double last_move, math::rng::RngEngine& engine);

/// Size of one occurrence, always >= 1. UniformIntSpec is uniform over
/// [min, max]; ExponentialSpec is exponential with the given mean, rounded
/// to the nearest whole share and floored at 1. Throws
/// std::invalid_argument for a UniformIntSpec with min < 1 or min > max, or
/// an ExponentialSpec whose mean is not finite and > 0.
[[nodiscard]] MMS_SIMULATION_EXPORT mm::Quantity draw_size(const DistributionSpec& spec,
                                                           math::rng::RngEngine& engine);

/// Everything adverse flow does to one asset in one tick, as ready-to-submit
/// MarketOrders (IOC). Uses the asset's adverse_flow_lambda,
/// adverse_flow_direction_correlation and adverse_flow_size. `last_move` is
/// this asset's price change over the previous tick; `dt` is the tick
/// length. Throws std::invalid_argument on any invalid input above.
[[nodiscard]] MMS_SIMULATION_EXPORT std::vector<mm::MarketOrder> generate_adverse_flow(
    const AssetSpec& asset, double last_move, double dt, math::rng::RngEngine& engine);

} // namespace mms::simulation

#endif // MMS_SIMULATION_ADVERSE_FLOW_HPP