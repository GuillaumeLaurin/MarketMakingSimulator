#ifndef MMS_SIMULATION_ADVERSE_FLOW_HPP
#define MMS_SIMULATION_ADVERSE_FLOW_HPP

/// \file
/// Adverse order flow: what hits the market maker's quotes on one asset
/// during one tick. Pure functions of their inputs (the last price move is
/// passed in, not remembered), so each can be tested with nothing but an
/// RngEngine. The flow is always a MarketOrder with IOC: it only ever takes
/// liquidity, it never rests.
///
/// The three draws it is made of (the arrival count, the direction, the size) live in
/// MMS::agents (agents/flow.hpp), where the agents that trade at random use them too; the
/// names are kept here. Draw order is fixed, which is what makes a run reproducible: first
/// the arrival count, then for each occurrence its direction and its size.

#include <vector>

#include <agents/flow.hpp>
#include <mm/types.hpp>

#include <math/rng/engine.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/simulation_export.hpp>
#include <simulation/size_spec.hpp>

namespace mms::simulation {

using agents::draw_arrival_count;
using agents::draw_direction;
using agents::draw_size;

/// Everything adverse flow does to one asset in one tick, as ready-to-submit
/// MarketOrders (IOC). Uses the asset's adverse_flow_lambda,
/// adverse_flow_direction_correlation and adverse_flow_size. `last_move` is
/// this asset's price change over the previous tick; `dt` is the tick
/// length. Throws std::invalid_argument on any invalid input above.
[[nodiscard]] MMS_SIMULATION_EXPORT std::vector<mm::MarketOrder> generate_adverse_flow(
    const AssetSpec& asset, double last_move, double dt, math::rng::RngEngine& engine);

} // namespace mms::simulation

#endif // MMS_SIMULATION_ADVERSE_FLOW_HPP
