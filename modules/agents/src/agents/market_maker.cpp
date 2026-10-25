/// \file
/// Implementation of MarketMakerAgent (<agents/market_maker.hpp>).

#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <agents/market_maker.hpp>
#include <mm/quote.hpp>
#include <mm/requote.hpp>

#include <math/types.hpp>

namespace mms::agents {

namespace {

constexpr std::size_t side_index(mm::Side side) noexcept {
    return side == mm::Side::Buy ? 0U : 1U;
}

// What the agent has at the exchange on one side of one asset.
struct SideOrders {
    std::vector<const OpenOrder*> resting;
    bool in_flight = false; ///< an order on its way, or a cancel on its way
};

using OwnOrders = std::vector<std::array<SideOrders, 2>>; ///< by asset, then side

[[noreturn]] void reject(const char* message) {
    throw std::invalid_argument(std::string("MarketMakerAgent: ") + message);
}

// The true price of every asset, from the signals.
math::Vector read_mids(const AgentView& view, std::size_t assets) {
    math::Vector mids(static_cast<Eigen::Index>(assets));
    for (std::size_t asset = 0; asset < assets; ++asset) {
        if (!view.assets[asset].signal.has_value()) {
            reject("every asset needs a signal: the market maker quotes around the true price");
        }
        mids(static_cast<Eigen::Index>(asset)) = *view.assets[asset].signal;
    }
    return mids;
}

// The agent's own orders, sorted by asset and side.
OwnOrders sort_own_orders(const AgentView& view, std::size_t assets) {
    OwnOrders own(assets);
    for (const OpenOrder& order : view.open_orders) {
        if (order.asset >= assets) {
            continue;
        }
        SideOrders& side = own[order.asset][side_index(order.side)];
        if (order.state == OrderState::Resting) {
            side.resting.push_back(&order);
        } else {
            side.in_flight = true;
        }
    }
    return own;
}

// The quote resting on a side: the oldest of the resting orders, if any.
std::optional<mm::LiveQuote> live_quote(const SideOrders& orders) {
    if (orders.resting.empty()) {
        return std::nullopt;
    }
    const OpenOrder& oldest = *orders.resting.front();
    return mm::LiveQuote{.id = oldest.id,
                         .quote = mm::Quote{.price = oldest.price, .quantity = oldest.remaining}};
}

// Finds what is resting, and keeps the requote plan off the sides that must be left alone:
// a side with something on its way keeps exactly what it has, and a side with more than one
// resting order is cleaned up (the oldest stays, the others are cancelled in `out`) and kept
// as it is for now.
mm::LiveQuoteSet reconcile(const OwnOrders& own, mm::QuoteSet& desired, std::vector<Action>& out) {
    mm::LiveQuoteSet live(own.size());
    for (std::size_t asset = 0; asset < own.size(); ++asset) {
        for (const mm::Side side : {mm::Side::Buy, mm::Side::Sell}) {
            const SideOrders& orders = own[asset][side_index(side)];
            const std::optional<mm::LiveQuote> quote = live_quote(orders);
            (side == mm::Side::Buy ? live[asset].bid : live[asset].ask) = quote;

            const bool crowded = orders.resting.size() > 1;
            for (std::size_t extra = 1; extra < orders.resting.size(); ++extra) {
                out.emplace_back(Cancel{.asset = asset, .id = orders.resting[extra]->id});
            }
            if (orders.in_flight || crowded) {
                desired[asset].on(side) =
                    quote.has_value() ? std::optional<mm::Quote>(quote->quote) : std::nullopt;
            }
        }
    }
    return live;
}

} // namespace

MarketMakerAgent::MarketMakerAgent(const MarketMakerSpec& spec)
    : strategy_(mm::make_strategy(spec.strategy)), tick_sizes_(spec.strategy.tick_sizes),
      threshold_(spec.requote_threshold_ticks) {
    if (threshold_ < 0) {
        reject("the requote threshold must be >= 0");
    }
}

void MarketMakerAgent::act(const AgentView& view,
                           math::rng::RngEngine& /*rng*/,
                           std::vector<Action>& out) {
    const std::size_t assets = strategy_->asset_count();
    if (view.assets.size() != assets || view.account.positions.size() != assets) {
        reject("the view must have one asset and one position per asset of the strategy");
    }

    const math::Vector mids = read_mids(view, assets);
    strategy_->on_market_data(mids, view.elapsed);
    const std::vector<mm::Quantity> positions(view.account.positions.begin(),
                                              view.account.positions.end());
    mm::QuoteSet desired = strategy_->quote(mm::make_market_view(mids, positions, view.time));

    const mm::LiveQuoteSet live = reconcile(sort_own_orders(view, assets), desired, out);
    const mm::RequotePlan plan = mm::plan_requote(desired, live, tick_sizes_, threshold_);

    for (const mm::CancelRequest& cancel : plan.cancels) {
        out.emplace_back(Cancel{.asset = cancel.asset, .id = cancel.id});
    }
    for (const mm::SubmitRequest& submit : plan.submits) {
        out.emplace_back(SubmitLimit{.asset = submit.asset,
                                     .side = submit.side,
                                     .price = submit.quote.price,
                                     .quantity = submit.quote.quantity,
                                     .time_in_force = mm::TimeInForce::GTC});
    }
}

} // namespace mms::agents
