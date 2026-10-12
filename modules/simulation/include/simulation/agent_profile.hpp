#ifndef MMS_SIMULATION_AGENT_PROFILE_HPP
#define MMS_SIMULATION_AGENT_PROFILE_HPP

/// \file
/// What every agent has in common, whatever it does in the market: who it is, what
/// it starts with, and what the exchange lets it do. The behaviour itself (a market
/// maker's strategy, a trend follower's lookback) is the agent's own and lives in
/// MMS::agents. The delays and the decision period are added when the loop learns
/// to run several agents.

#include <optional>
#include <string>

#include <mm/types.hpp>

namespace mms::simulation {

/// Plain data, no invariant enforced here: Account validates it when it is built,
/// the same way every spec in this module defers to wherever the context to check
/// it against exists.
struct AgentProfile {
    /// What this agent is called. It is its identity: its random stream is derived
    /// from it (math::rng::asset_key_of, under Subsystem::Agent), so adding,
    /// removing or reordering agents changes nothing about the others. Must be
    /// non-empty and unique in a simulation; World checks.
    std::string name;

    /// Cash the agent starts with. Not negative. A market maker that is to quote
    /// both sides needs enough of it: a refused quote is simply not posted.
    double initial_cash;

    /// May the agent sell what it does not hold? A short sale needs collateral
    /// (AccountRules::short_collateral_rate). The market maker needs this.
    bool can_short = false;

    /// Largest |position| allowed on any one asset. Absent means no limit. Orders
    /// that bring the position down are always accepted, even past the limit.
    std::optional<mm::Quantity> max_abs_position = std::nullopt;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_AGENT_PROFILE_HPP