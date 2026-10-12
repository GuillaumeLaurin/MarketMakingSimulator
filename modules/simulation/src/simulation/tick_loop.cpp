/// \file
/// Implementation of TickLoop (<simulation/tick_loop.hpp>).

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <agents/agent.hpp>
#include <mm/quote.hpp>

#include <simulation/adverse_flow.hpp>
#include <simulation/latency.hpp>
#include <simulation/tick_loop.hpp>

#include "math/rng/engine.hpp"
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

/// Does this kind of agent get the true price as the signal of its view?
bool gets_signal(const agents::AgentBehavior& behavior) {
    return std::holds_alternative<agents::MarketMakerSpec>(behavior) ||
           std::holds_alternative<agents::InformedFlowSpec>(behavior);
}

/// What the engine needs to be given for one submit, and what the loop needs to know of it.
struct Sent {
    mm::AssetId asset = 0;
    mm::Side side = mm::Side::Buy;
    mm::Quantity quantity = 0;
    mm::Order order;
    bool can_rest = false;
};

Sent describe(const SubmitArrival& arrival) {
    return std::visit(
        [](const auto& request) -> Sent {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (std::is_same_v<Request, agents::SubmitLimit>) {
                return Sent{.asset = request.asset,
                            .side = request.side,
                            .quantity = request.quantity,
                            .order = mm::LimitOrder{.side = request.side,
                                                    .price = request.price,
                                                    .quantity = request.quantity,
                                                    .time_in_force = request.time_in_force},
                            .can_rest = request.time_in_force == mm::TimeInForce::GTC};
            } else {
                return Sent{.asset = request.asset,
                            .side = request.side,
                            .quantity = request.quantity,
                            .order = mm::MarketOrder{.side = request.side,
                                                     .quantity = request.quantity,
                                                     .time_in_force = request.time_in_force},
                            .can_rest = false};
            }
        },
        arrival.order);
}

} // namespace

/// One per asset: turns what happens in that asset's book into the loop's own bookkeeping
/// (accounts, owners, observers).
class TickLoop::EngineObserver final : public mm::MatchingEngineObserver {
public:
    EngineObserver(TickLoop& loop, mm::AssetId asset) : loop_(loop), asset_(asset) {}

    void on_trade(const mm::Trade& trade) override { loop_.on_trade(asset_, trade); }

    void on_order_cancelled(mm::OrderId id) override { loop_.on_order_cancelled(asset_, id); }

private:
    TickLoop& loop_;
    mm::AssetId asset_;
};

TickLoop::TickLoop(World& world, const SimulationConfig& config)
    : world_(world), dt_(config.dt), latency_(config.latency), assets_(config.assets),
      previous_prices_(world.price_process().prices()),
      quotes_changed_(world.asset_count(), false) {
    if (!std::isfinite(dt_) || dt_ <= 0.0) {
        reject("dt must be finite and > 0");
    }
    if (assets_.size() != world.asset_count()) {
        reject("the config does not match the number of assets of the world");
    }
    validate_delay_spec(latency_.adverse_flow_delay);

    for (std::size_t i = 0; i < world.agent_count(); ++i) {
        const agents::AgentBehavior& behavior = config.agents.at(i).behavior;
        shown_signal_.push_back(gets_signal(behavior));
        if (!observed_maker_.has_value() &&
            std::holds_alternative<agents::MarketMakerSpec>(behavior)) {
            observed_maker_ = i;
        }
    }

    for (mm::AssetId asset = 0; asset < world.asset_count(); ++asset) {
        // A dry run on a COPY of the engine: it validates the whole
        // adverse-flow spec now, without consuming the real stream.
        math::rng::RngEngine probe = world.adverse_flow_engine(asset);
        static_cast<void>(generate_adverse_flow(assets_[asset], 0.0, dt_, probe));

        engine_observers_.push_back(std::make_unique<EngineObserver>(*this, asset));
        mm::MatchingEngine& engine = world.matching_engine(asset);
        engine.add_observer(*engine_observers_.back());
        engine.set_self_trade_prevention([this, asset](mm::OrderId id) {
            if (!submission_.has_value()) {
                return false;
            }
            const std::optional<OrderOwner> owner = registry_.find(asset, id);
            return owner.has_value() && owner->agent == submission_->agent;
        });

        plain_markets_.push_back(agents::AssetView{.bids = {},
                                                   .asks = {},
                                                   .recent_trades = {},
                                                   .signal = std::nullopt,
                                                   .tick_size = assets_[asset].tick_size});
    }
    signal_markets_ = plain_markets_;
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
        for (std::size_t agent = 0; agent < world_.agent_count(); ++agent) {
            world_.agent(agent).on_mark(asset, mid);
        }
        for (SimulationObserver* observer : observers_) {
            observer->on_mark(asset, mid);
        }
    }

    // Adverse flow is scheduled BEFORE the agents act: at equal due
    // times it is therefore executed first, against the previous tick's quotes.
    schedule_adverse_flow(prices);
    run_agents(prices);
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

void TickLoop::run_agents(const math::Vector& prices) {
    for (std::size_t asset = 0; asset < signal_markets_.size(); ++asset) {
        signal_markets_[asset].signal = prices(static_cast<Eigen::Index>(asset));
    }

    for (std::size_t agent = 0; agent < world_.agent_count(); ++agent) {
        AgentRuntime& runtime = world_.agent(agent);
        if (!runtime.due(time_)) {
            continue;
        }
        const ActResult result =
            runtime.act(ticks_, time_, shown_signal_[agent] ? signal_markets_ : plain_markets_);
        for (const Scheduled& scheduled : result.scheduled) {
            std::visit(
                [&](const auto& event) { world_.event_queue().schedule(scheduled.due, event); },
                scheduled.event);
        }
    }
}

void TickLoop::execute(const SimulationEvent& event) {
    std::visit(
        [this](const auto& request) {
            using Request = std::decay_t<decltype(request)>;

            if constexpr (std::is_same_v<Request, AdverseOrder>) {
                static_cast<void>(
                    world_.matching_engine(request.asset).submit(mm::Order{request.order}));
            } else if constexpr (std::is_same_v<Request, SubmitArrival>) {
                execute_submit(request);
            } else {
                static_assert(std::is_same_v<Request, CancelArrival>);
                execute_cancel(request);
            }
        },
        event);
}

void TickLoop::execute_submit(const SubmitArrival& arrival) {
    AgentRuntime& runtime = world_.agent(arrival.agent);
    const Sent sent = describe(arrival);

    submission_ = Submission{.agent = arrival.agent, .ticket = arrival.ticket, .side = sent.side};
    const mm::OrderId id = world_.matching_engine(sent.asset).submit(sent.order);
    const mm::Quantity remaining = sent.quantity - submission_->filled;
    submission_.reset();

    if (sent.can_rest && remaining > 0) {
        registry_.add(sent.asset, id, OrderOwner{.agent = arrival.agent, .ticket = arrival.ticket});
        runtime.on_arrived(arrival.ticket, id);
    } else {
        // An immediate order that did not rest, or one that traded in full (then it is
        // closed already and this does nothing).
        static_cast<void>(runtime.on_done(arrival.ticket));
    }
    touch(arrival.agent, sent.asset);
}

void TickLoop::execute_cancel(const CancelArrival& arrival) {
    // False when the order was filled away before this arrived. The engine reports a
    // cancel it carries out, which is where the order is closed.
    static_cast<void>(world_.matching_engine(arrival.asset).cancel(arrival.id));
}

std::optional<OrderOwner>
TickLoop::owner_of(mm::AssetId asset, mm::OrderId id, mm::Side side) const {
    if (const std::optional<OrderOwner> owner = registry_.find(asset, id)) {
        return owner;
    }
    // An id that is not recorded is the order being submitted right now, on its side.
    if (submission_.has_value() && submission_->side == side) {
        return OrderOwner{.agent = submission_->agent, .ticket = submission_->ticket};
    }
    return std::nullopt;
}

void TickLoop::on_trade(mm::AssetId asset, const mm::Trade& trade) {
    const auto settle = [&](mm::OrderId id, mm::Side side) {
        const std::optional<OrderOwner> owner = owner_of(asset, id, side);
        if (!owner.has_value()) {
            return; // adverse flow
        }
        AgentRuntime& runtime = world_.agent(owner->agent);
        const bool aggressor = !registry_.find(asset, id).has_value();
        runtime.on_fill(owner->ticket,
                        agents::Fill{.asset = asset,
                                     .id = id,
                                     .side = side,
                                     .price = trade.price,
                                     .quantity = trade.quantity,
                                     .aggressor = aggressor});
        if (aggressor && submission_.has_value()) {
            submission_->filled += trade.quantity;
        } else if (!runtime.orders().ticket_of(asset, id).has_value()) {
            registry_.remove(asset, id); // filled in full
        }
        touch(owner->agent, asset);
    };
    settle(trade.buy_order_id, mm::Side::Buy);
    settle(trade.sell_order_id, mm::Side::Sell);

    for (SimulationObserver* observer : observers_) {
        observer->on_trade(asset, trade, submission_.has_value());
    }
}

void TickLoop::on_order_cancelled(mm::AssetId asset, mm::OrderId id) {
    const std::optional<OrderOwner> owner = registry_.find(asset, id);
    if (!owner.has_value()) {
        return;
    }
    static_cast<void>(world_.agent(owner->agent).on_done(owner->ticket));
    registry_.remove(asset, id);
    touch(owner->agent, asset);
}

void TickLoop::touch(AgentId agent, mm::AssetId asset) {
    if (observed_maker_ == agent) {
        quotes_changed_[asset] = true;
    }
}

mm::LiveQuoteSet TickLoop::live_quotes() const {
    mm::LiveQuoteSet live(world_.asset_count());
    if (!observed_maker_.has_value()) {
        return live;
    }
    for (const agents::OpenOrder& order : world_.agent(*observed_maker_).orders().orders()) {
        if (order.state == agents::OrderState::Submitting) {
            continue;
        }
        std::optional<mm::LiveQuote>& slot =
            order.side == mm::Side::Buy ? live[order.asset].bid : live[order.asset].ask;
        if (!slot.has_value()) {
            slot = mm::LiveQuote{.id = order.id,
                                 .quote =
                                     mm::Quote{.price = order.price, .quantity = order.remaining}};
        }
    }
    return live;
}

void TickLoop::notify_quote_changes() {
    const mm::LiveQuoteSet live = live_quotes();
    for (mm::AssetId asset = 0; asset < world_.asset_count(); ++asset) {
        if (!quotes_changed_[asset]) {
            continue;
        }
        quotes_changed_[asset] = false;
        const mm::AssetLiveQuotes& resting = live[asset];
        mm::AssetQuotes quotes;
        if (resting.bid.has_value()) {
            quotes.bid = resting.bid->quote;
        }
        if (resting.ask.has_value()) {
            quotes.ask = resting.ask->quote;
        }
        for (SimulationObserver* observer : observers_) {
            observer->on_quote(asset, quotes);
        }
    }
}

} // namespace mms::simulation
