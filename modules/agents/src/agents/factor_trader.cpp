/// \file
/// Implementation of FactorTraderAgent (<agents/factor_trader.hpp>).

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <agents/factor_trader.hpp>
#include <agents/sizing.hpp>

namespace mms::agents {

namespace {

[[noreturn]] void reject(const char* message) {
    throw std::invalid_argument(std::string("FactorTraderAgent: ") + message);
}

} // namespace

FactorTraderAgent::FactorTraderAgent(const FactorTraderSpec& spec) : spec_(spec) {
    if (spec.sensitivities.empty()) {
        reject("a factor trader needs at least one sensitivity");
    }
    for (const double sensitivity : spec.sensitivities) {
        if (!std::isfinite(sensitivity)) {
            reject("every sensitivity must be finite");
        }
    }
    if (!std::isfinite(spec.threshold) || spec.threshold < 0.0) {
        reject("the threshold must be finite and >= 0");
    }
    if (spec.order_size < 1 || spec.max_position < 1) {
        reject("the order size and the position limit must be at least 1");
    }
}

void FactorTraderAgent::act(const AgentView& view,
                            math::rng::RngEngine& /*rng*/,
                            std::vector<Action>& out) {
    if (view.factors.size() != spec_.sensitivities.size()) {
        reject("the view must give one factor move per sensitivity: is the agent informed?");
    }
    if (view.account.positions.size() != view.assets.size()) {
        reject("the view must have one position per asset");
    }

    double score = 0.0;
    for (std::size_t factor = 0; factor < view.factors.size(); ++factor) {
        if (!std::isfinite(view.factors[factor])) {
            reject("every factor move must be finite");
        }
        score += spec_.sensitivities[factor] * view.factors[factor];
    }
    if (std::abs(score) <= spec_.threshold) {
        return;
    }

    const mm::Side side = score > 0.0 ? mm::Side::Buy : mm::Side::Sell;
    for (std::size_t asset = 0; asset < view.assets.size(); ++asset) {
        const mm::Quantity quantity = clip_to_limit(
            side, view.account.positions[asset], spec_.order_size, spec_.max_position);
        if (quantity > 0) {
            out.emplace_back(SubmitMarket{.asset = asset,
                                          .side = side,
                                          .quantity = quantity,
                                          .time_in_force = mm::MarketTimeInForce::IOC});
        }
    }
}

} // namespace mms::agents
