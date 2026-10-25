/// \file
/// Implementation of InformedFlowAgent (<agents/informed_flow.hpp>).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <agents/flow.hpp>
#include <agents/informed_flow.hpp>
#include <agents/state_bytes.hpp>

namespace mms::agents {

namespace {

[[noreturn]] void reject(const char* message) {
    throw std::invalid_argument(std::string("InformedFlowAgent: ") + message);
}

} // namespace

InformedFlowAgent::InformedFlowAgent(const InformedFlowSpec& spec) : spec_(spec) {
    if (!std::isfinite(spec.lambda) || spec.lambda < 0.0) {
        reject("lambda must be finite and >= 0");
    }
    validate_correlation(spec.direction_correlation);
    validate_size_spec(spec.size);
}

void InformedFlowAgent::act(const AgentView& view,
                            math::rng::RngEngine& rng,
                            std::vector<Action>& out) {
    const std::size_t assets = view.assets.size();
    if (!previous_.empty() && previous_.size() != assets) {
        reject("the number of assets must not change between two acts");
    }

    // Everything is checked before the first draw, so a rejected call leaves the engine as it was.
    std::vector<double> signals(assets);
    for (std::size_t asset = 0; asset < assets; ++asset) {
        if (!view.assets[asset].signal.has_value() || !std::isfinite(*view.assets[asset].signal)) {
            reject("every asset needs a finite signal: the informed trader knows the true price");
        }
        signals[asset] = *view.assets[asset].signal;
    }

    for (std::size_t asset = 0; asset < assets; ++asset) {
        const double last_move = previous_.empty() ? 0.0 : signals[asset] - previous_[asset];
        const std::int64_t arrivals = draw_arrival_count(spec_.lambda, view.elapsed, rng);
        for (std::int64_t i = 0; i < arrivals; ++i) {
            const mm::Side side = draw_direction(spec_.direction_correlation, last_move, rng);
            const mm::Quantity quantity = draw_size(spec_.size, rng);
            out.emplace_back(SubmitMarket{.asset = asset,
                                          .side = side,
                                          .quantity = quantity,
                                          .time_in_force = mm::MarketTimeInForce::IOC});
        }
    }
    previous_ = std::move(signals);
}

std::vector<std::byte> InformedFlowAgent::save() const {
    return previous_.empty() ? std::vector<std::byte>{} : encode_doubles(previous_);
}

void InformedFlowAgent::load(std::span<const std::byte> state) {
    if (state.empty()) {
        previous_.clear();
        return;
    }
    previous_ = decode_doubles(state);
}

} // namespace mms::agents
