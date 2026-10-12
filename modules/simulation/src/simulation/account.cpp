/// \file
/// Implementation of Account (<simulation/account.hpp>).

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <mm/risk.hpp>

#include <simulation/account.hpp>

#include "agents/action.hpp"
#include "agents/reject_reason.hpp"
#include "mm/pnl.hpp"
#include "mm/types.hpp"
#include "simulation/account_rules.hpp"
#include "simulation/agent_profile.hpp"

namespace mms::simulation {

namespace {

[[noreturn]] void reject(const std::string& message) {
    throw std::invalid_argument("Account: " + message);
}

bool finite_and_not_negative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

AgentProfile validated(AgentProfile profile) {
    if (profile.name.empty()) {
        reject("an agent needs a name");
    }
    if (!finite_and_not_negative(profile.initial_cash)) {
        reject("initial_cash must be finite and >= 0");
    }
    if (profile.max_abs_position.has_value() && *profile.max_abs_position < 0) {
        reject("max_abs_position must be >= 0");
    }
    return profile;
}

AccountRules validated(const AccountRules& rules) {
    if (!finite_and_not_negative(rules.short_collateral_rate)) {
        reject("short_collateral_rate must be finite and >= 0");
    }
    if (!finite_and_not_negative(rules.market_order_buffer)) {
        reject("market_order_buffer must be finite and >= 0");
    }
    return rules;
}

// The part of an order that opens or adds to a position, as opposed to the part
// that only closes or reduces one. `against` is the quantity the order can reduce.
mm::Quantity opening_part(mm::Quantity quantity, mm::Quantity against) {
    return quantity - std::min(quantity, std::max<mm::Quantity>(0, against));
}

std::optional<agents::RejectReason> check_order(const Account& account,
                                                mm::AssetId asset,
                                                mm::Side side,
                                                mm::Quantity quantity,
                                                std::optional<mm::Price> limit_price) {
    const mm::PnlLedger& ledger = account.ledger();
    if (asset >= ledger.asset_count()) {
        return agents::RejectReason::UnknownAsset;
    }
    if (quantity <= 0 ||
        (limit_price.has_value() && (!std::isfinite(*limit_price) || *limit_price <= 0.0))) {
        return agents::RejectReason::InvalidOrder;
    }

    const mm::Quantity position = ledger.position(asset).quantity();
    const mm::Price mid = ledger.last_mid(asset);
    const std::optional<mm::Quantity>& limit = account.profile().max_abs_position;

    if (side == mm::Side::Buy) {
        const mm::Quantity effective = position + account.pending_buy(asset);
        if (limit.has_value() &&
            !mm::position_limit_allows(mm::to_double(effective), side, quantity, *limit)) {
            return agents::RejectReason::PositionLimit;
        }
        // Buying back a short position never needs cash.
        const mm::Quantity open = opening_part(quantity, -position);
        if (open > 0) {
            const mm::Price price =
                limit_price.value_or(mid * (1.0 + account.rules().market_order_buffer));
            if (account.buying_power() - (price * mm::to_double(open)) < 0.0) {
                return agents::RejectReason::InsufficientCash;
            }
        }
        return std::nullopt;
    }

    const mm::Quantity effective = position - account.pending_sell(asset);
    if (limit.has_value() &&
        !mm::position_limit_allows(mm::to_double(effective), side, quantity, *limit)) {
        return agents::RejectReason::PositionLimit;
    }
    // Stock already promised to a pending sale cannot be sold twice.
    const mm::Quantity open_short = opening_part(quantity, effective);
    if (open_short > 0) {
        if (!account.profile().can_short) {
            return agents::RejectReason::ShortNotAllowed;
        }
        const double needed =
            account.rules().short_collateral_rate * mm::to_double(open_short) * mid;
        if (account.buying_power() - needed < 0.0) {
            return agents::RejectReason::InsufficientCollateral;
        }
    }
    return std::nullopt;
}

} // namespace

Account::Account(AgentProfile profile,
                 AccountRules rules,
                 const std::vector<mm::LedgerAsset>& assets)
    : profile_(validated(std::move(profile))), rules_(validated(rules)), ledger_(assets),
      cash_(profile_.initial_cash) {
}

double Account::reserved_cash() const {
    double total = 0.0;
    for (const auto& [ticket, pending] : pending_) {
        if (pending.side == mm::Side::Buy) {
            total += pending.reserved_price * mm::to_double(pending.remaining);
        }
    }
    return total;
}

double Account::collateral() const {
    std::vector<mm::Quantity> selling(ledger_.asset_count(), 0);
    for (const auto& [ticket, pending] : pending_) {
        if (pending.side == mm::Side::Sell) {
            selling[pending.asset] += pending.remaining;
        }
    }
    double value = 0.0;
    for (mm::AssetId asset = 0; asset < selling.size(); ++asset) {
        // The most the agent could be short once its pending sales fill.
        const mm::Quantity short_by =
            std::max<mm::Quantity>(0, selling[asset] - ledger_.position(asset).quantity());
        value += mm::to_double(short_by) * ledger_.last_mid(asset);
    }
    return rules_.short_collateral_rate * value;
}

double Account::buying_power() const {
    return cash_ - reserved_cash() - collateral();
}

mm::Quantity Account::pending_buy(mm::AssetId asset) const {
    static_cast<void>(ledger_.position(asset)); // throws std::out_of_range for an unknown asset
    mm::Quantity total = 0;
    for (const auto& [ticket, pending] : pending_) {
        if (pending.asset == asset && pending.side == mm::Side::Buy) {
            total += pending.remaining;
        }
    }
    return total;
}

mm::Quantity Account::pending_sell(mm::AssetId asset) const {
    static_cast<void>(ledger_.position(asset)); // throws std::out_of_range for an unknown asset
    mm::Quantity total = 0;
    for (const auto& [ticket, pending] : pending_) {
        if (pending.asset == asset && pending.side == mm::Side::Sell) {
            total += pending.remaining;
        }
    }
    return total;
}

std::optional<agents::RejectReason> Account::check(const agents::Action& action) const {
    return std::visit(
        [this](const auto& order) -> std::optional<agents::RejectReason> {
            using Order = std::decay_t<decltype(order)>;
            if constexpr (std::is_same_v<Order, agents::SubmitLimit>) {
                return check_order(*this, order.asset, order.side, order.quantity, order.price);
            } else if constexpr (std::is_same_v<Order, agents::SubmitMarket>) {
                return check_order(*this, order.asset, order.side, order.quantity, std::nullopt);
            } else {
                static_assert(std::is_same_v<Order, agents::Cancel>);
                return std::nullopt; // whether the order exists is for the loop to say
            }
        },
        action);
}

Ticket Account::open(const Pending& pending) {
    if (pending.asset >= ledger_.asset_count()) {
        throw std::out_of_range("Account: no such asset");
    }
    if (pending.remaining <= 0) {
        throw std::invalid_argument("Account: an order needs a quantity > 0");
    }
    const Ticket ticket{next_ticket_++};
    pending_.emplace(ticket, pending);
    return ticket;
}

Ticket Account::accept(const agents::SubmitLimit& order) {
    return open(Pending{.asset = order.asset,
                        .side = order.side,
                        .reserved_price = order.price,
                        .remaining = order.quantity});
}

Ticket Account::accept(const agents::SubmitMarket& order) {
    if (order.asset >= ledger_.asset_count()) {
        throw std::out_of_range("Account: no such asset");
    }
    const mm::Price mid = ledger_.last_mid(order.asset);
    const mm::Price estimate =
        order.side == mm::Side::Buy ? mid * (1.0 + rules_.market_order_buffer) : mid;
    return open(Pending{.asset = order.asset,
                        .side = order.side,
                        .reserved_price = estimate,
                        .remaining = order.quantity});
}

void Account::on_fill(Ticket ticket, mm::Price price, mm::Quantity quantity) {
    Pending& pending = pending_.at(ticket);
    if (quantity <= 0 || quantity > pending.remaining) {
        throw std::invalid_argument("Account: a fill must be > 0 and no more than what is left");
    }

    // The ledger validates the fill and changes nothing when it throws, so it goes first.
    const double fees_before = ledger_.breakdown(pending.asset).fees;
    ledger_.on_fill(pending.asset, pending.side, price, quantity, ledger_.last_mid(pending.asset));
    const double fee_change = ledger_.breakdown(pending.asset).fees - fees_before;

    const double notional = price * mm::to_double(quantity);
    cash_ += (pending.side == mm::Side::Buy ? -notional : notional) + fee_change;

    pending.remaining -= quantity;
    if (pending.remaining == 0) {
        pending_.erase(ticket);
    }
}

bool Account::release(Ticket ticket) {
    return pending_.erase(ticket) > 0;
}

} // namespace mms::simulation