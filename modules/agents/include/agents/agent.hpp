#ifndef MMS_AGENTS_AGENT_HPP
#define MMS_AGENTS_AGENT_HPP

/// \file
/// Agent: anything that trades in the market, bot, market maker or, one day,
/// a person. The loop calls act() when the agent's decision period comes
/// round, and tells it about its fills and refusals as they happen.
///
/// An agent must be deterministic: no clock, no threads, no random numbers
/// of its own. Randomness comes from the engine passed to act(), which the
/// world owns, so the run replays exactly and save/resume stays exact.

#include <cstddef>
#include <span>
#include <vector>

#include <agents/action.hpp>
#include <agents/agent_view.hpp>
#include <agents/reject_reason.hpp>
#include <mm/types.hpp>

#include <math/rng/engine.hpp>

namespace mms::agents {

/// A fill of one of the agent's orders.
struct Fill {
    mm::AssetId asset;
    mm::OrderId id;
    mm::Side side;
    mm::Price price;
    mm::Quantity quantity;
    /// True when the agent's own order started the trade.
    bool aggressor;
};

class Agent {
public:
    virtual ~Agent() = default;

    /// Appends the actions to take now to `out`, which the caller has emptied.
    /// Appending nothing is the usual answer.
    virtual void
    act(const AgentView& view, math::rng::RngEngine& rng, std::vector<Action>& out) = 0;

    /// One of this agent's orders traded. Called before the next act().
    virtual void on_fill(const Fill& /*fill*/) {}

    /// The gateway refused `action`, for `reason`. Called at once, within the
    /// act() that produced it.
    virtual void on_reject(const Action& /*action*/, RejectReason /*reason*/) {}

    /// Everything the agent needs to carry on exactly where it is, as bytes
    /// only it understands. Empty for an agent with no state.
    [[nodiscard]] virtual std::vector<std::byte> save() const { return {}; }

    /// Restores what save() produced. Called on a freshly built agent.
    virtual void load(std::span<const std::byte> /*state*/) {}
};

} // namespace mms::agents

#endif // MMS_AGENTS_AGENT_HPP