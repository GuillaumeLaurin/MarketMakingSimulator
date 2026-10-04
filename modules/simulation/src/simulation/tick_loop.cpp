/// \file
/// Implementation of TickLoop (<simulation/tick_loop.hpp>).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <mm/quote.hpp>
#include <mm/quoting_strategy.hpp>

#include <simulation/adverse_flow.hpp>
#include <simulation/latency.hpp>
#include <simulation/tick_loop.hpp>

#include "math/rng/engine.hpp"
#include "mm/requote.hpp"
#include "mm/types.hpp"
#include "simulation/latency_config.hpp"
#include "simulation/simulation_config.hpp"
#include "simulation/simulation_observer.hpp"
#include "simulation/world.hpp"

namespace mms::simulation {

namespace {

[[noreturn]] void reject(const char* message) {
    throw std::invalid_argument(std::string("TickLoop: ") + message);
}

constexpr std::size_t side_index(mm::Side side) noexcept {
    return side == mm::Side::Buy ? 0U : 1U;
}

std::optional<mm::LiveQuote>& live_slot(mm::LiveQuoteSet& live, mm::AssetId asset, mm::Side side) {
    return side == mm::Side::Buy ? live[asset].bid : live[asset].ask;
}

/// The side whose live quote carries `id`. A cancel in a plan always names a
/// quote that was live when the plan was made.
mm::Side side_of(const mm::LiveQuoteSet& live, const mm::CancelRequest& cancel) {
    const mm::AssetLiveQuotes& quotes = live[cancel.asset];
    if (quotes.bid.has_value() && quotes.bid->id == cancel.id) {
        return mm::Side::Buy;
    }
    if (quotes.ask.has_value() && quotes.ask->id == cancel.id) {
        return mm::Side::Sell;
    }
    throw std::logic_error("TickLoop: a planned cancel names a quote that is not live");
}

mm::AssetQuotes as_quotes(const mm::AssetLiveQuotes& live) {
    mm::AssetQuotes quotes;
    if (live.bid.has_value()) {
        quotes.bid = live.bid->quote;
    }
    if (live.ask.has_value()) {
        quotes.ask = live.ask->quote;
    }
    return quotes;
}

} // namespace

/// One per asset: turns a trade in that asset's book into the loop's own
/// bookkeeping (ledger, live quotes, observers).
class TickLoop::EngineObserver final : public mm::MatchingEngineObserver {
public:
    EngineObserver(TickLoop& loop, mm::AssetId asset) : loop_(loop), asset_(asset) {}

    void on_trade(const mm::Trade& trade) override { loop_.on_trade(asset_, trade); }

private:
    TickLoop& loop_;
    mm::AssetId asset_;
};

TickLoop::TickLoop(World& world, const SimulationConfig& config)
    : world_(world), dt_(config.dt), requote_threshold_(config.requote_threshold_ticks),
      tick_sizes_(config.strategy.tick_sizes), latency_(config.latency), assets_(config.assets),
      previous_prices_(world.price_process().prices()), live_(world.asset_count()),
      in_flight_(world.asset_count(), std::vector<int>(2, 0)),
      quotes_changed_(world.asset_count(), false) {
    if (!std::isfinite(dt_) || dt_ <= 0.0) {
        reject("dt must be finite and > 0");
    }
    if (requote_threshold_ < 0) {
        reject("the requote threshold must be >= 0");
    }
    if (assets_.size() != world.asset_count() || tick_sizes_.size() != world.asset_count()) {
        reject("the config does not match the number of assets of the world");
    }
    validate_delay_spec(latency_.quote_delay);
    validate_delay_spec(latency_.adverse_flow_delay);

    for (mm::AssetId asset = 0; asset < world.asset_count(); ++asset) {
        // A dry run on a COPY of the engine: it validates the whole
        // adverse-flow spec now, without consuming the real stream.
        math::rng::RngEngine probe = world.adverse_flow_engine(asset);
        static_cast<void>(generate_adverse_flow(assets_[asset], 0.0, dt_, probe));

        engine_observers_.push_back(std::make_unique<EngineObserver>(*this, asset));
        world.matching_engine(asset).add_observer(*engine_observers_.back());
    }

    world.strategy().on_market_data(world.price_process().prices(), dt_);
}

TickLoop::~TickLoop() = default;

void TickLoop::add_observer(SimulationObserver& observer) {
    observers_.push_back(&observer);
}

void TickLoop::step() {
    ++ticks_;
    time_ = static_cast<double>(ticks_) * dt_;
    for (SimulationObserver* observer : observers_) {
        observer->on_tick_start(time_);
    }

    world_.price_process().advance(dt_);
    const math::Vector prices = world_.price_process().prices();
    for (mm::AssetId asset = 0; asset < world_.asset_count(); ++asset) {
        const mm::Price mid = prices(static_cast<Eigen::Index>(asset));
        world_.pnl_ledger().on_mark(asset, mid);
        for (SimulationObserver* observer : observers_) {
            observer->on_mark(asset, mid);
        }
    }

    world_.strategy().on_market_data(prices, dt_);

    // Adverse flow is scheduled BEFORE the quote requests: at equal due
    // times it is therefore executed first, against the previous tick's quotes.
    schedule_adverse_flow(prices);
    schedule_requests(prices);
    previous_prices_ = prices;

    for (const SimulationEvent& event : world_.event_queue().drain_until(time_)) {
        execute(event);
    }

    notify_quote_changes();
    for (SimulationObserver* observer : observers_) {
        observer->on_tick_end(time_);
    }
}

void TickLoop::schedule_adverse_flow(const math::Vector& prices) {
    for (mm::AssetId asset = 0; asset < world_.asset_count(); ++asset) {
        const auto index = static_cast<Eigen::Index>(asset);
        const double last_move = prices(index) - previous_prices_(index);

        const std::vector<mm::MarketOrder> orders = generate_adverse_flow(
            assets_[asset], last_move, dt_, world_.adverse_flow_engine(asset));
        for (const mm::MarketOrder& order : orders) {
            const double delay =
                draw_delay(latency_.adverse_flow_delay, world_.latency_engine(asset));
            world_.event_queue().schedule(time_ + delay,
                                          AdverseOrder{.asset = asset, .order = order});
        }
    }
}

void TickLoop::schedule_requests(const math::Vector& prices) {
    const mm::MarketView view =
        mm::make_market_view(prices, world_.pnl_ledger().quantities(), time_);
    const mm::QuoteSet desired = world_.strategy().quote(view);
    const mm::RequotePlan plan = mm::plan_requote(desired, live_, tick_sizes_, requote_threshold_);

    // A side with a request already in flight is left alone, judged on the
    // counts BEFORE this plan adds its own requests (a replace sends two).
    const std::vector<std::vector<int>> busy = in_flight_;
    std::vector<double> cancel_due(2U * world_.asset_count(), 0.0);

    for (const mm::CancelRequest& cancel : plan.cancels) {
        const mm::Side side = side_of(live_, cancel);
        if (busy[cancel.asset][side_index(side)] > 0) {
            continue;
        }
        const double due =
            time_ + draw_delay(latency_.quote_delay, world_.latency_engine(cancel.asset));
        world_.event_queue().schedule(due, cancel);
        ++in_flight_[cancel.asset][side_index(side)];
        cancel_sides_[{cancel.asset, cancel.id}] = side;
        cancel_due[(2U * cancel.asset) + side_index(side)] = due;
    }

    for (const mm::SubmitRequest& submit : plan.submits) {
        if (busy[submit.asset][side_index(submit.side)] > 0) {
            continue;
        }
        // A replacement never overtakes its own cancel: the loop tracks ONE
        // order per (asset, side), and ties are first in, first out.
        const double due =
            std::max(time_ + draw_delay(latency_.quote_delay, world_.latency_engine(submit.asset)),
                     cancel_due[(2U * submit.asset) + side_index(submit.side)]);
        world_.event_queue().schedule(due, submit);
        ++in_flight_[submit.asset][side_index(submit.side)];
    }
}

void TickLoop::execute(const SimulationEvent& event) {
    std::visit(
        [this](const auto& request) {
            using Request = std::decay_t<decltype(request)>;

            if constexpr (std::is_same_v<Request, AdverseOrder>) {
                static_cast<void>(
                    world_.matching_engine(request.asset).submit(mm::Order{request.order}));
            } else if constexpr (std::is_same_v<Request, mm::SubmitRequest>) {
                std::optional<mm::LiveQuote>& slot = live_slot(live_, request.asset, request.side);
                if (slot.has_value()) {
                    throw std::logic_error("TickLoop: a submit reached an occupied side");
                }

                submitting_side_ = request.side;
                submit_filled_ = 0;
                const mm::OrderId id = world_.matching_engine(request.asset)
                                           .submit(mm::to_limit_order(request.side, request.quote));
                submitting_side_.reset();

                // What a self-trade took during the submit is no longer resting.
                const mm::Quantity remaining = request.quote.quantity - submit_filled_;
                if (remaining > 0) {
                    slot = mm::LiveQuote{
                        .id = id,
                        .quote = mm::Quote{.price = request.quote.price, .quantity = remaining}};
                }
                quotes_changed_[request.asset] = true;
                --in_flight_[request.asset][side_index(request.side)];
            } else {
                static_assert(std::is_same_v<Request, mm::CancelRequest>);
                // False when the quote was filled away before this arrived.
                static_cast<void>(world_.matching_engine(request.asset).cancel(request.id));

                const mm::Side side = cancel_sides_.at({request.asset, request.id});
                cancel_sides_.erase({request.asset, request.id});
                std::optional<mm::LiveQuote>& slot = live_slot(live_, request.asset, side);
                if (slot.has_value() && slot->id == request.id) {
                    slot.reset();
                    quotes_changed_[request.asset] = true;
                }
                --in_flight_[request.asset][side_index(side)];
            }
        },
        event);
}

void TickLoop::on_trade(mm::AssetId asset, const mm::Trade& trade) {
    std::optional<mm::LiveQuote>& bid = live_[asset].bid;
    std::optional<mm::LiveQuote>& ask = live_[asset].ask;

    const bool bid_was_resting = bid.has_value() && bid->id == trade.buy_order_id;
    const bool ask_was_resting = ask.has_value() && ask->id == trade.sell_order_id;

    // An id that is not a live quote belongs to adverse flow, unless one of
    // the market maker's own submits is executing right now: then the
    // unknown id on that side is its new order, and it crossed its own quote.
    const bool buy_is_mm = bid_was_resting || submitting_side_ == mm::Side::Buy;
    const bool sell_is_mm = ask_was_resting || submitting_side_ == mm::Side::Sell;
    const bool aggressor_is_mm = submitting_side_.has_value();

    const mm::Price mid = world_.price_process().prices()(static_cast<Eigen::Index>(asset));

    const auto book_fill =
        [&](mm::Side side, std::optional<mm::LiveQuote>& slot, bool was_resting) {
            world_.pnl_ledger().on_fill(asset, side, trade.price, trade.quantity, mid);
            if (was_resting) {
                slot->quote.quantity -= trade.quantity;
                if (slot->quote.quantity <= 0) {
                    slot.reset();
                }
                quotes_changed_[asset] = true;
            } else {
                submit_filled_ += trade.quantity;
            }
        };
    if (buy_is_mm) {
        book_fill(mm::Side::Buy, bid, bid_was_resting);
    }
    if (sell_is_mm) {
        book_fill(mm::Side::Sell, ask, ask_was_resting);
    }

    for (SimulationObserver* observer : observers_) {
        observer->on_trade(asset, trade, aggressor_is_mm);
    }
}

void TickLoop::notify_quote_changes() {
    for (mm::AssetId asset = 0; asset < world_.asset_count(); ++asset) {
        if (!quotes_changed_[asset]) {
            continue;
        }
        quotes_changed_[asset] = false;
        const mm::AssetQuotes quotes = as_quotes(live_[asset]);
        for (SimulationObserver* observer : observers_) {
            observer->on_quote(asset, quotes);
        }
    }
}

} // namespace mms::simulation
