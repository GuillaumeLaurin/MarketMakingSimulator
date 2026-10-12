#ifndef MMS_AGENTS_MARKET_MAKER_HPP
#define MMS_AGENTS_MARKET_MAKER_HPP

/// \file
/// MarketMakerAgent: the quoting strategy of MMS::mm, working as an agent. Each time it
/// acts it reads the true price of every asset from AssetView::signal, feeds it to the
/// strategy, asks the strategy for the quotes it wants, and sends only what changed:
/// the requote plan of mm decides which resting quote to replace.
///
/// It keeps no record of its own orders. What it has in the market is read from
/// AgentView::open_orders, with the rule the market maker has always followed: a side that
/// has an order on its way, or a cancel on its way, is left alone until that has landed.
/// A side that holds more than one resting order (it should not happen) is cleaned up
/// first, keeping the oldest.
///
/// The market maker must be given a signal for every asset (see AssetView::signal): it
/// quotes around the true price, and there is nothing else to read a price from while it is
/// the only one quoting. The state of the strategy's volatility estimators is not saved by
/// save() yet, so a run resumed from a saved agent restarts them.

#include <memory>
#include <vector>

#include <agents/agent.hpp>
#include <agents/agents_export.hpp>
#include <mm/quoting_strategy.hpp>
#include <mm/strategy_config.hpp>
#include <mm/tick.hpp>
#include <mm/types.hpp>

namespace mms::agents {

struct MarketMakerSpec {
    /// How it quotes: the strategy, its risk limits, its sizes and tick sizes, one entry per
    /// asset. The assets are the ones of the simulation, in order.
    mm::StrategyConfig strategy;

    /// A resting quote is replaced only once its price has moved by at least this many
    /// ticks (or its size changed). 0 replaces every quote at every act. Not negative.
    mm::Ticks requote_threshold_ticks = 1;
};

class MMS_AGENTS_EXPORT MarketMakerAgent final : public Agent {
public:
    /// Builds the strategy from `spec`. Throws std::invalid_argument for a negative
    /// threshold, and for anything mm::make_strategy rejects.
    explicit MarketMakerAgent(const MarketMakerSpec& spec);

    /// Throws std::invalid_argument when the view does not have one asset and one position
    /// per asset of the strategy, when an asset has no signal, or when view.elapsed is not
    /// finite and > 0 (the strategy divides returns by its square root).
    void act(const AgentView& view, math::rng::RngEngine& rng, std::vector<Action>& out) override;

private:
    std::unique_ptr<mm::QuotingStrategy> strategy_;
    std::vector<mm::Price> tick_sizes_;
    mm::Ticks threshold_;
};

} // namespace mms::agents

#endif // MMS_AGENTS_MARKET_MAKER_HPP
