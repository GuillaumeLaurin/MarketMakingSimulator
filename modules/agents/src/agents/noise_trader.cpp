/// \file
/// Implementation of NoiseTraderAgent (<agents/noise_trader.hpp>).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <agents/book.hpp>
#include <agents/flow.hpp>
#include <agents/noise_trader.hpp>
#include <mm/tick.hpp>

#include <math/dist/discrete.hpp>
#include <math/dist/uniform.hpp>

namespace mms::agents {

namespace {

[[noreturn]] void reject(const char* message) {
    throw std::invalid_argument(std::string("NoiseTraderAgent: ") + message);
}

// What one arrival is, before the book has a say.
struct Arrival {
    mm::Side side;
    mm::Quantity quantity;
    bool at_market;
    mm::Ticks offset;
};

// All four are drawn whatever comes of them, so the stream follows the arrivals.
Arrival draw_one(const NoiseTraderSpec& spec, math::rng::RngEngine& rng) {
    const mm::Side side = draw_direction(0.0, 0.0, rng);
    const mm::Quantity quantity = draw_size(spec.size, rng);
    const bool at_market = math::dist::bernoulli(rng, spec.market_probability);
    const mm::Ticks offset = math::dist::uniform_int(rng, 1, spec.max_offset_ticks);
    return Arrival{.side = side, .quantity = quantity, .at_market = at_market, .offset = offset};
}

// The order an arrival becomes on an asset whose price is `mid`: a market order, or a passive
// limit order a few ticks off the middle. Nothing for a price that would be zero or less.
std::optional<Action>
make_order(const Arrival& arrival, std::size_t asset, mm::Price tick_size, mm::Price mid) {
    if (arrival.at_market) {
        return SubmitMarket{.asset = asset,
                            .side = arrival.side,
                            .quantity = arrival.quantity,
                            .time_in_force = mm::MarketTimeInForce::IOC};
    }
    const mm::Ticks centre = mm::nearest_ticks(mid, tick_size);
    const mm::Ticks wanted =
        arrival.side == mm::Side::Buy ? centre - arrival.offset : centre + arrival.offset;
    if (wanted <= 0) {
        return std::nullopt;
    }
    return SubmitLimit{.asset = asset,
                       .side = arrival.side,
                       .price = mm::price_at(wanted, tick_size),
                       .quantity = arrival.quantity,
                       .time_in_force = mm::TimeInForce::GTC};
}

// Cancels the oldest resting orders until the passive ones, those it has and those it is
// about to send, are within `cap`. An order on its way counts but cannot be cancelled; one
// being cancelled does not count.
void trim_to_cap(const AgentView& view,
                 std::size_t new_passive,
                 std::size_t cap,
                 std::vector<Action>& out) {
    std::size_t open = 0;
    for (const OpenOrder& order : view.open_orders) {
        open += order.state == OrderState::Cancelling ? 0U : 1U;
    }
    std::size_t excess = (open + new_passive > cap) ? open + new_passive - cap : 0U;
    for (const OpenOrder& order : view.open_orders) {
        if (excess == 0) {
            break;
        }
        if (order.state == OrderState::Resting) {
            out.emplace_back(Cancel{.asset = order.asset, .id = order.id});
            --excess;
        }
    }
}

} // namespace

NoiseTraderAgent::NoiseTraderAgent(const NoiseTraderSpec& spec) : spec_(spec) {
    if (!std::isfinite(spec.lambda) || spec.lambda < 0.0) {
        reject("lambda must be finite and >= 0");
    }
    if (!std::isfinite(spec.market_probability) || spec.market_probability < 0.0 ||
        spec.market_probability > 1.0) {
        reject("the market probability must be finite and in [0, 1]");
    }
    if (spec.max_offset_ticks < 1) {
        reject("a passive order sits at least one tick from the middle");
    }
    validate_size_spec(spec.size);
}

void NoiseTraderAgent::act(const AgentView& view,
                           math::rng::RngEngine& rng,
                           std::vector<Action>& out) {
    // Everything is checked before the first draw, so a rejected call leaves the engine as it was.
    for (const AssetView& asset : view.assets) {
        if (!std::isfinite(asset.tick_size) || asset.tick_size <= 0.0) {
            reject("every asset needs a tick size that is finite and > 0");
        }
    }

    std::size_t new_passive = 0;
    for (std::size_t asset = 0; asset < view.assets.size(); ++asset) {
        const AssetView& market = view.assets[asset];
        const std::optional<mm::Price> mid = book_mid(market);
        const std::int64_t arrivals = draw_arrival_count(spec_.lambda, view.elapsed, rng);
        for (std::int64_t i = 0; i < arrivals; ++i) {
            const Arrival arrival = draw_one(spec_, rng);
            if (!mid.has_value()) {
                continue;
            }
            const std::optional<Action> order = make_order(arrival, asset, market.tick_size, *mid);
            if (!order.has_value()) {
                continue;
            }
            out.push_back(*order);
            new_passive += std::holds_alternative<SubmitLimit>(*order) ? 1U : 0U;
        }
    }
    trim_to_cap(view, new_passive, spec_.max_resting_orders, out);
}

} // namespace mms::agents
