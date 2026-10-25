/// \file
/// Implementation of validate_agent_setup (<simulation/config_validation.hpp>).

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include <math/rng/stream.hpp>
#include <simulation/config_validation.hpp>
#include <simulation/latency.hpp>

namespace mms::simulation {

namespace {

[[noreturn]] void reject(const std::string& message) {
    throw std::invalid_argument("SimulationConfig: " + message);
}

bool finite_and_not_negative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

void validate_assets(const SimulationConfig& config) {
    for (const AssetSpec& asset : config.assets) {
        if (!std::isfinite(asset.tick_size) || asset.tick_size <= 0.0) {
            reject("the tick size of " + asset.symbol + " must be finite and > 0");
        }
    }
}

void validate_exchange(const SimulationConfig& config) {
    if (config.book_depth < 1) {
        reject("book_depth must be >= 1");
    }
    if (!finite_and_not_negative(config.account_rules.short_collateral_rate) ||
        !finite_and_not_negative(config.account_rules.market_order_buffer)) {
        reject("the account rules must be finite and >= 0");
    }
}

void validate_agents(const SimulationConfig& config) {
    std::unordered_set<std::uint64_t> names;
    for (const AgentSpec& agent : config.agents) {
        const std::string& name = agent.profile.name;
        if (name.empty()) {
            reject("every agent needs a name");
        }
        if (!names.insert(static_cast<std::uint64_t>(math::rng::asset_key_of(name))).second) {
            reject("two agents share the name " + name);
        }
        const double period = agent.timing.decision_period;
        if (!std::isfinite(period) || period <= 0.0) {
            reject("the decision period of " + name + " must be finite and > 0");
        }
        validate_delay_spec(agent.timing.order_delay);
        validate_delay_spec(agent.timing.info_delay);
    }
}

} // namespace

void validate_agent_setup(const SimulationConfig& config) {
    validate_assets(config);
    validate_exchange(config);
    validate_agents(config);
}

} // namespace mms::simulation
