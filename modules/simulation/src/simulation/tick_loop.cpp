/// \file
/// Implementation of TickLoop (<simulation/tick_loop.hpp>).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <agents/agent.hpp>
#include <mm/quote.hpp>

#include <simulation/tick_loop.hpp>

#include "math/rng/engine.hpp"
#include "mm/types.hpp"
#include "simulation/simulation_config.hpp"
#include "simulation/simulation_observer.hpp"
#include "simulation/world.hpp"

namespace mms::simulation {

namespace {

[[noreturn]] void reject(const char* message) {
    throw std::invalid_argument(std::string("TickLoop: ") + message);
}

/// The most trades of one asset an agent is shown at a time.
constexpr std::size_t recent_trades_limit = 50;

double validated_dt(double dt) {
    if (!std::isfinite(dt) || dt <= 0.0) {
        reject("dt must be finite and > 0");
    }
    return dt;
}

/// Does this kind of agent get the true price as the signal of its view?
bool gets_signal(const agents::AgentBehavior& behavior) {
    return std::holds_alternative<agents::MarketMakerSpec>(behavior) ||
           std::holds_alternative<agents::InformedFlowSpec>(behavior);
}

/// The longest an information delay can be, for sizing the history. An exponential delay has no
/// longest: 30 means covers all but one draw in 10^13.
double longest_delay(const DelaySpec& spec) {
    return std::visit(
        [](const auto& delay) -> double {
            using Spec = std::decay_t<decltype(delay)>;
            if constexpr (std::is_same_v<Spec, ConstantDelaySpec>) {
                return delay.delay;
            } else if constexpr (std::is_same_v<Spec, UniformDelaySpec>) {
                return delay.max;
            } else {
                return 30.0 * delay.mean;
            }
        },
        spec);
}

/// How many ticks of length dt cover `span`, with a cap against absurd specs.
std::size_t ticks_covering(double span, double dt) {
    constexpr double cap = 10'000'000.0;
    return static_cast<std::size_t>(std::min(std::ceil(span / dt), cap));
}

/// The history an agent setup needs: snapshots as far back as the longest information delay,
/// tape as far back as the longest delay plus the longest decision period.
MarketHistory history_for(const SimulationConfig& config) {
    std::vector<mm::Price> tick_sizes;
    tick_sizes.reserve(config.assets.size());
    for (const AssetSpec& asset : config.assets) {
        tick_sizes.push_back(asset.tick_size);
    }
    double delay = 0.0;
    double period = 0.0;
    for (const AgentSpec& agent : config.agents) {
        delay = std::max(delay, longest_delay(agent.timing.info_delay));
        period = std::max(period, agent.timing.decision_period);
    }
    const double dt = config.dt;
    return MarketHistory(std::move(tick_sizes),
                         ticks_covering(delay, dt) + 1,
                         ticks_covering(delay + period, dt) + 2,
                         recent_trades_limit);
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
    : world_(world), dt_(validated_dt(config.dt)), history_(history_for(config)),
      book_depth_(static_cast<std::size_t>(std::max(config.book_depth, 1))),
      quotes_changed_(world.asset_count(), false) {
    if (config.assets.size() != world.asset_count()) {
        reject("the config does not match the number of assets of the world");
    }

    for (std::size_t i = 0; i < world.agent_count(); ++i) {
        const agents::AgentBehavior& behavior = config.agents.at(i).behavior;
        shown_signal_.push_back(gets_signal(behavior));
        shown_factors_.push_back(std::holds_alternative<agents::FactorTraderSpec>(behavior));
        seen_.push_back(0);
        if (!observed_maker_.has_value() &&
            std::holds_alternative<agents::MarketMakerSpec>(behavior)) {
            observed_maker_ = i;
        }
    }

    for (mm::AssetId asset = 0; asset < world.asset_count(); ++asset) {
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
    }
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

    record_market(prices);
    run_agents();

    for (const SimulationEvent& event : world_.event_queue().drain_until(time_)) {
        execute(event);
    }

    notify_quote_changes();
    for (SimulationObserver* observer : observers_) {
        observer->on_tick_end(time_);
    }
}

void TickLoop::record_market(const math::Vector& prices) {
    const std::size_t assets = world_.asset_count();
    std::vector<std::vector<agents::BookLevel>> bids(assets);
    std::vector<std::vector<agents::BookLevel>> asks(assets);
    for (mm::AssetId asset = 0; asset < assets; ++asset) {
        const mm::OrderBook& book = world_.matching_engine(asset).book();
        for (const auto& level : book.levels(mm::Side::Buy, book_depth_)) {
            bids[asset].push_back({.price = level.price, .quantity = level.quantity});
        }
        for (const auto& level : book.levels(mm::Side::Sell, book_depth_)) {
            asks[asset].push_back({.price = level.price, .quantity = level.quantity});
        }
    }
    history_.record(
        time_, std::span<const double>(prices.data(), assets), std::move(bids), std::move(asks));

    // The true moves of the common factors go to the agents that are shown them, who add
    // them up until they act.
    const math::Vector& moves = world_.price_process().factor_increments();
    if (moves.size() > 0) {
        for (std::size_t agent = 0; agent < world_.agent_count(); ++agent) {
            if (shown_factors_[agent]) {
                world_.agent(agent).add_factors(
                    std::span<const double>(moves.data(), static_cast<std::size_t>(moves.size())));
            }
        }
    }
}

void TickLoop::run_agents() {
    for (std::size_t agent = 0; agent < world_.agent_count(); ++agent) {
        AgentRuntime& runtime = world_.agent(agent);
        if (!runtime.due(time_)) {
            continue;
        }
        // The market as it was `delay` ago.
        const double delay = runtime.draw_info_delay();
        const std::span<const agents::AssetView> markets =
            history_.view(time_ - delay, shown_signal_[agent], seen_[agent]);
        const ActResult result = runtime.act(ticks_, time_, markets);
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

            if constexpr (std::is_same_v<Request, SubmitArrival>) {
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

std::optional<OrderOwner> TickLoop::owner_of(mm::AssetId asset, mm::OrderId id) const {
    if (const std::optional<OrderOwner> owner = registry_.find(asset, id)) {
        return owner;
    }
    // An id that is not recorded is the order being submitted right now.
    if (submission_.has_value()) {
        return OrderOwner{.agent = submission_->agent, .ticket = submission_->ticket};
    }
    return std::nullopt;
}

void TickLoop::on_trade(mm::AssetId asset, const mm::Trade& trade) {
    history_.add_trade(asset,
                       agents::TradePrint{.price = trade.price,
                                          .quantity = trade.quantity,
                                          .aggressor = submission_.value().side});
    const bool started_by_maker = submission_.has_value() && observed_maker_ == submission_->agent;
    const auto settle = [&](mm::OrderId id, mm::Side side) {
        // Every order in a book belongs to an agent: no owner would be a bug, and throws.
        const OrderOwner owner = owner_of(asset, id).value();
        AgentRuntime& runtime = world_.agent(owner.agent);
        const bool aggressor = !registry_.find(asset, id).has_value();
        runtime.on_fill(owner.ticket,
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
        touch(owner.agent, asset);
    };
    settle(trade.buy_order_id, mm::Side::Buy);
    settle(trade.sell_order_id, mm::Side::Sell);

    for (SimulationObserver* observer : observers_) {
        observer->on_trade(asset, trade, started_by_maker);
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
