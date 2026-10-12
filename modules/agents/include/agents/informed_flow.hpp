#ifndef MMS_AGENTS_INFORMED_FLOW_HPP
#define MMS_AGENTS_INFORMED_FLOW_HPP

/// \file
/// InformedFlowAgent: what used to be called adverse order flow, as an agent. It knows the
/// true price (AssetView::signal), and trades in the direction the price has just gone, with
/// market orders that only ever take liquidity. It is the one that hits the market maker's
/// quotes once the price has moved past them: the adverse selection a market maker has to
/// live with.
///
/// On each asset, at each act: a Poisson number of orders arrives, with mean
/// lambda * elapsed. Each one follows the last move of the signal with probability
/// `direction_correlation` (up means buy, down means sell) and is a fair coin otherwise, and
/// has a size drawn from `size`. Every order is an immediate-or-cancel market order. The
/// last move is the change of the signal since the agent's previous act, zero at the first.
///
/// The draws are those of agents/flow.hpp, in their fixed order, asset by asset. The agent
/// remembers the last signal of each asset, and saves it.
///
/// Its orders are subject to the rules of the account like anyone's: it needs cash for the
/// shares it buys, and the right to sell short and collateral for the ones it sells.

#include <cstddef>
#include <span>
#include <vector>

#include <agents/agent.hpp>
#include <agents/agents_export.hpp>
#include <agents/size_spec.hpp>

namespace mms::agents {

struct InformedFlowSpec {
    /// Orders per unit of simulation time, on each asset. Finite and not negative.
    double lambda;
    /// How surely an order follows the last move, in [0, 1]. 0 is pure noise.
    double direction_correlation;
    /// The size of one order.
    DistributionSpec size;
};

class MMS_AGENTS_EXPORT InformedFlowAgent final : public Agent {
public:
    /// Throws std::invalid_argument for a lambda that is not finite and >= 0, a correlation
    /// outside [0, 1], or a size spec that is not valid (agents/flow.hpp).
    explicit InformedFlowAgent(const InformedFlowSpec& spec);

    /// Throws std::invalid_argument when an asset has no signal, when the number of assets
    /// changes from one act to the next, and when elapsed is not finite and > 0 or
    /// lambda * elapsed is more than 1e6. Nothing is drawn when it throws.
    void act(const AgentView& view, math::rng::RngEngine& rng, std::vector<Action>& out) override;

    /// The last signal of each asset, as bytes of this machine. Empty before the first act.
    [[nodiscard]] std::vector<std::byte> save() const override;

    /// Restores what save() produced. Throws std::invalid_argument for bytes that are not
    /// that: nothing is changed then.
    void load(std::span<const std::byte> state) override;

private:
    InformedFlowSpec spec_;
    std::vector<double> previous_; ///< last signal per asset; empty until the first act
};

} // namespace mms::agents

#endif // MMS_AGENTS_INFORMED_FLOW_HPP
