#ifndef MMS_AGENTS_NOISE_TRADER_HPP
#define MMS_AGENTS_NOISE_TRADER_HPP

/// \file
/// NoiseTraderAgent: someone who trades for reasons that have nothing to do with the price.
/// On each asset, at each act, a Poisson number of orders arrives (mean lambda * elapsed).
/// Each is a buy or a sell on a fair coin, has a size drawn from `size`, and is, with
/// probability `market_probability`, a market order that takes what the book offers, and
/// otherwise a passive limit order, 1 to `max_offset_ticks` ticks away from the middle of the
/// book on its own side (a buy below the middle, a sell above it). The passive orders are what
/// puts liquidity in the book other than the market maker's.
///
/// It reads the price from the book (agents/book.hpp) and does nothing on an asset whose book
/// has no middle and no last trade yet. Its draws do not depend on that: every arrival draws
/// its direction, size, kind and offset, so the random stream follows the arrivals and not the
/// state of the book.
///
/// Passive orders would pile up forever, so the agent keeps at most `max_resting_orders` of
/// them in all: when it would be over, it cancels the oldest ones first.
///
/// Its orders are subject to the rules of the account like anyone's.

#include <cstddef>
#include <vector>

#include <agents/agent.hpp>
#include <agents/agents_export.hpp>
#include <agents/size_spec.hpp>
#include <mm/tick.hpp>

namespace mms::agents {

struct NoiseTraderSpec {
    /// Orders per unit of simulation time, on each asset. Finite and not negative.
    double lambda;
    /// The size of one order.
    DistributionSpec size;
    /// The chance that an order is a market order, in [0, 1].
    double market_probability;
    /// A passive order sits between 1 and this many ticks from the middle. At least 1.
    mm::Ticks max_offset_ticks;
    /// How many passive orders it keeps at most, over all its assets.
    std::size_t max_resting_orders = 20;
};

class MMS_AGENTS_EXPORT NoiseTraderAgent final : public Agent {
public:
    /// Throws std::invalid_argument for a lambda that is not finite and >= 0, a probability
    /// outside [0, 1], an offset below 1, or a size spec that is not valid.
    explicit NoiseTraderAgent(const NoiseTraderSpec& spec);

    /// Throws std::invalid_argument when an asset has a tick size that is not finite and > 0,
    /// and when elapsed is not finite and > 0 or lambda * elapsed is more than 1e6. Nothing
    /// is drawn when it throws.
    void act(const AgentView& view, math::rng::RngEngine& rng, std::vector<Action>& out) override;

private:
    NoiseTraderSpec spec_;
};

} // namespace mms::agents

#endif // MMS_AGENTS_NOISE_TRADER_HPP
