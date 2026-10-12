/// \file
/// Implementation of adverse flow (<simulation/adverse_flow.hpp>).

#include <cstddef>
#include <cstdint>
#include <vector>

#include <agents/flow.hpp>

#include <simulation/adverse_flow.hpp>

#include "mm/types.hpp"

namespace mms::simulation {

std::vector<mm::MarketOrder> generate_adverse_flow(const AssetSpec& asset,
                                                   double last_move,
                                                   double dt,
                                                   math::rng::RngEngine& engine) {
    // Everything is validated before the first draw, so a rejected call
    // leaves the engine exactly where it was.
    agents::validate_correlation(asset.adverse_flow_direction_correlation);
    agents::validate_last_move(last_move);
    agents::validate_size_spec(asset.adverse_flow_size);

    const std::int64_t count = agents::draw_arrival_count(asset.adverse_flow_lambda, dt, engine);

    std::vector<mm::MarketOrder> orders;
    orders.reserve(static_cast<std::size_t>(count));
    for (std::int64_t i = 0; i < count; ++i) {
        const mm::Side side =
            agents::draw_direction(asset.adverse_flow_direction_correlation, last_move, engine);
        const mm::Quantity quantity = agents::draw_size(asset.adverse_flow_size, engine);
        orders.push_back(mm::MarketOrder{
            .side = side, .quantity = quantity, .time_in_force = mm::MarketTimeInForce::IOC});
    }
    return orders;
}

} // namespace mms::simulation
