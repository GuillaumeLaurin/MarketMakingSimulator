/// \file
/// Implementation of DeviationTraderAgent (<agents/deviation_trader.hpp>).

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include <agents/book.hpp>
#include <agents/deviation_trader.hpp>
#include <agents/sizing.hpp>
#include <agents/state_bytes.hpp>

namespace mms::agents {

namespace {

constexpr double not_yet = std::numeric_limits<double>::quiet_NaN();

[[noreturn]] void reject(const char* message) {
    throw std::invalid_argument(std::string("DeviationTraderAgent: ") + message);
}

} // namespace

DeviationTraderAgent::DeviationTraderAgent(const DeviationParams& params, Stance stance)
    : params_(params), stance_(stance), weight_(0.0) {
    if (!std::isfinite(params.half_life) || params.half_life <= 0.0) {
        reject("the half-life must be finite and > 0");
    }
    if (!std::isfinite(params.threshold) || params.threshold < 0.0) {
        reject("the threshold must be finite and >= 0");
    }
    if (params.order_size < 1 || params.max_position < 1) {
        reject("the order size and the position limit must be at least 1");
    }
    // After one half-life the weight of the past has halved: (1 - w)^half_life = 1/2.
    weight_ = -std::expm1(-std::numbers::ln2 / params.half_life);
}

void DeviationTraderAgent::act(const AgentView& view,
                               math::rng::RngEngine& /*rng*/,
                               std::vector<Action>& out) {
    const std::size_t assets = view.assets.size();
    if (view.account.positions.size() != assets) {
        reject("the view must have one position per asset");
    }
    if (!average_.empty() && average_.size() != assets) {
        reject("the number of assets must not change between two acts");
    }
    if (average_.empty()) {
        average_.assign(assets, not_yet);
    }

    for (std::size_t asset = 0; asset < assets; ++asset) {
        const std::optional<mm::Price> price = book_mid(view.assets[asset]);
        if (!price.has_value() || !(*price > 0.0)) {
            continue;
        }
        double& average = average_[asset];
        if (std::isnan(average)) {
            average = *price;
            continue;
        }

        const double deviation = (*price / average) - 1.0;
        average += weight_ * (*price - average);
        if (std::abs(deviation) <= params_.threshold) {
            continue;
        }

        const bool rising = deviation > 0.0;
        const bool buys = rising == (stance_ == Stance::Follow);
        const mm::Side side = buys ? mm::Side::Buy : mm::Side::Sell;
        const mm::Quantity quantity = clip_to_limit(
            side, view.account.positions[asset], params_.order_size, params_.max_position);
        if (quantity > 0) {
            out.emplace_back(SubmitMarket{.asset = asset,
                                          .side = side,
                                          .quantity = quantity,
                                          .time_in_force = mm::MarketTimeInForce::IOC});
        }
    }
}

std::vector<std::byte> DeviationTraderAgent::save() const {
    return average_.empty() ? std::vector<std::byte>{} : encode_doubles(average_);
}

void DeviationTraderAgent::load(std::span<const std::byte> state) {
    if (state.empty()) {
        average_.clear();
        return;
    }
    average_ = decode_doubles(state);
}

} // namespace mms::agents
