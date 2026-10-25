#ifndef MMS_AGENTS_FACTOR_TRADER_HPP
#define MMS_AGENTS_FACTOR_TRADER_HPP

/// \file
/// FactorTraderAgent: someone who trades the market on the moves of its common factors. The
/// price process of a factor model moves every asset with a few shared factors; this agent knows
/// how those factors moved (AgentView::factors, for agents configured as informed) and has an
/// opinion of its own on each, its sensitivity.
///
/// At each act it adds up the moves of the factors since its last act, weighted by its
/// sensitivities: `score = sum over factors of sensitivity * move`. A score above `threshold`
/// in either direction is a reason to trade: positive buys every asset, negative sells every
/// asset, one market order of `order_size` shares each, cut down so that no position goes past
/// plus or minus `max_position`. A sensitivity of zero ignores a factor, a negative one bets
/// against it.
///
/// It draws no random number and keeps no state.
///
/// Its orders are subject to the rules of the account like anyone's.

#include <cstddef>
#include <vector>

#include <agents/agent.hpp>
#include <agents/agents_export.hpp>
#include <mm/types.hpp>

namespace mms::agents {

struct FactorTraderSpec {
    /// One entry per factor of the price process, in the process's order. Each finite.
    std::vector<double> sensitivities;
    /// How far the score has to go, either way, before it trades. Finite and >= 0.
    double threshold;
    /// Shares per trade, per asset. At least 1.
    mm::Quantity order_size;
    /// Largest position it will hold in any asset, long or short. At least 1.
    mm::Quantity max_position;
};

class MMS_AGENTS_EXPORT FactorTraderAgent final : public Agent {
public:
    /// Throws std::invalid_argument for no sensitivities, one that is not finite, a threshold
    /// that is not finite and >= 0, or an order size or a position limit below 1.
    explicit FactorTraderAgent(const FactorTraderSpec& spec);

    /// Throws std::invalid_argument when the view does not give one move per sensitivity (the
    /// agent is not configured as informed, or the process has other factors), or does not have
    /// one position per asset, or a move is not finite.
    void act(const AgentView& view, math::rng::RngEngine& rng, std::vector<Action>& out) override;

private:
    FactorTraderSpec spec_;
};

} // namespace mms::agents

#endif // MMS_AGENTS_FACTOR_TRADER_HPP
