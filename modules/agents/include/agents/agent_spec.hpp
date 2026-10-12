#ifndef MMS_AGENTS_AGENT_SPEC_HPP
#define MMS_AGENTS_AGENT_SPEC_HPP

/// \file
/// What an agent does, as data, and the one place that turns it into a live Agent. The
/// per-agent settings that are the same for every agent (name, cash, how often it acts,
/// its delays) are in MMS::simulation; this is only the behavior.

#include <memory>
#include <variant>

#include <agents/agent.hpp>
#include <agents/agents_export.hpp>
#include <agents/informed_flow.hpp>
#include <agents/market_maker.hpp>
#include <agents/noise_trader.hpp>

namespace mms::agents {

/// One alternative per kind of agent.
using AgentBehavior = std::variant<MarketMakerSpec, InformedFlowSpec, NoiseTraderSpec>;

/// Builds the agent `behavior` describes. Throws whatever the agent's constructor throws
/// for a spec it rejects (std::invalid_argument).
[[nodiscard]] MMS_AGENTS_EXPORT std::unique_ptr<Agent> make_agent(const AgentBehavior& behavior);

} // namespace mms::agents

#endif // MMS_AGENTS_AGENT_SPEC_HPP
