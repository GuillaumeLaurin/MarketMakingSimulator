/// \file
/// Implementation of AgentRuntime (<simulation/agent_runtime.hpp>).

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <agents/agent_spec.hpp>

#include <simulation/agent_runtime.hpp>
#include <simulation/latency.hpp>

namespace mms::simulation {

namespace {

/// Slack on the decision clock, relative to the period.
constexpr double clock_slack = 1e-9;

/// `due`, or later if a cancel of the same asset and side is still to arrive by then.
double after_cancels(const ActResult& result, mm::AssetId asset, mm::Side side, double due) {
    for (const Scheduled& scheduled : result.scheduled) {
        const auto* cancel = std::get_if<CancelArrival>(&scheduled.event);
        if (cancel != nullptr && cancel->asset == asset && cancel->side == side) {
            due = std::max(due, scheduled.due);
        }
    }
    return due;
}

} // namespace

AgentRuntime::AgentRuntime(AgentId id,
                           const AgentSpec& spec,
                           const AccountRules& rules,
                           const std::vector<mm::LedgerAsset>& assets,
                           math::rng::RngEngine decision,
                           math::rng::RngEngine latency)
    : AgentRuntime(id, spec, rules, assets, decision, latency, agents::make_agent(spec.behavior)) {
}

AgentRuntime::AgentRuntime(AgentId id,
                           const AgentSpec& spec,
                           const AccountRules& rules,
                           const std::vector<mm::LedgerAsset>& assets,
                           math::rng::RngEngine decision,
                           math::rng::RngEngine latency,
                           std::unique_ptr<agents::Agent> agent)
    : id_(id), spec_(spec), agent_(std::move(agent)), account_(spec.profile, rules, assets),
      decision_(decision), latency_(latency) {
    if (agent_ == nullptr) {
        throw std::invalid_argument("AgentRuntime: there is no agent");
    }
}

double AgentRuntime::draw_info_delay() {
    return draw_delay(spec_.timing.info_delay, latency_);
}

bool AgentRuntime::due(double time) const noexcept {
    return time >= next_due_ - (clock_slack * spec_.timing.decision_period);
}

void AgentRuntime::add_factors(std::span<const double> increments) {
    if (factors_.empty()) {
        factors_.assign(increments.size(), 0.0);
    }
    if (increments.size() != factors_.size()) {
        throw std::invalid_argument("AgentRuntime: the number of factors changed");
    }
    for (std::size_t i = 0; i < increments.size(); ++i) {
        factors_[i] += increments[i];
    }
}

ActResult
AgentRuntime::act(std::int64_t tick, double time, std::span<const agents::AssetView> markets) {
    positions_ = account_.ledger().quantities();
    const agents::AgentView view{.tick = tick,
                                 .time = time,
                                 .elapsed = time - last_act_,
                                 .account = {.cash = account_.cash(),
                                             .buying_power = account_.buying_power(),
                                             .positions = positions_},
                                 .open_orders = orders_.orders(),
                                 .assets = markets,
                                 .factors = factors_};

    std::vector<agents::Action> actions;
    agent_->act(view, decision_, actions);

    last_act_ = time;
    next_due_ = time + spec_.timing.decision_period;
    std::ranges::fill(factors_, 0.0);

    ActResult result;
    for (const agents::Action& action : actions) {
        const double delay = draw_delay(spec_.timing.order_delay, latency_);
        send(action, time, delay, result);
    }
    return result;
}

void AgentRuntime::send(const agents::Action& action, double now, double delay, ActResult& result) {
    if (const auto* cancel = std::get_if<agents::Cancel>(&action)) {
        const auto listed =
            std::ranges::find_if(orders_.orders(), [&](const agents::OpenOrder& order) {
                return order.asset == cancel->asset && order.id == cancel->id;
            });
        if (listed != orders_.orders().end() && orders_.request_cancel(cancel->asset, cancel->id)) {
            result.scheduled.push_back({.due = now + delay,
                                        .event = CancelArrival{.agent = id_,
                                                               .asset = cancel->asset,
                                                               .id = cancel->id,
                                                               .side = listed->side}});
        }
        return;
    }

    if (const std::optional<agents::RejectReason> reason = account_.check(action)) {
        agent_->on_reject(action, *reason);
        result.refused.push_back({.action = action, .reason = *reason});
        return;
    }

    if (const auto* limit = std::get_if<agents::SubmitLimit>(&action)) {
        const Ticket ticket = account_.accept(*limit);
        orders_.on_accepted(ticket, limit->asset, limit->side, limit->price, limit->quantity);
        result.scheduled.push_back(
            {.due = after_cancels(result, limit->asset, limit->side, now + delay),
             .event = SubmitArrival{.agent = id_, .ticket = ticket, .order = *limit}});
    } else {
        const auto& market = std::get<agents::SubmitMarket>(action);
        const Ticket ticket = account_.accept(market);
        // A market order has no price: the list shows it as 0.
        orders_.on_accepted(ticket, market.asset, market.side, 0.0, market.quantity);
        result.scheduled.push_back(
            {.due = after_cancels(result, market.asset, market.side, now + delay),
             .event = SubmitArrival{.agent = id_, .ticket = ticket, .order = market}});
    }
}

void AgentRuntime::on_arrived(Ticket ticket, mm::OrderId id) {
    orders_.on_arrived(ticket, id);
}

void AgentRuntime::on_fill(Ticket ticket, const agents::Fill& fill) {
    account_.on_fill(ticket, fill.price, fill.quantity);
    orders_.on_filled(ticket, fill.quantity);
    agent_->on_fill(fill);
}

bool AgentRuntime::on_done(Ticket ticket) {
    const bool released = account_.release(ticket);
    static_cast<void>(orders_.on_done(ticket)); // the account and the list close together
    return released;
}

} // namespace mms::simulation
