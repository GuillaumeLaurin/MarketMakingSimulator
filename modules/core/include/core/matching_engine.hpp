#ifndef MMS_CORE_MATCHING_ENGINE_HPP
#define MMS_CORE_MATCHING_ENGINE_HPP

/// \file
/// MatchingEngine: the ACTIVE algorithm. Takes a new Order (any of the 5
/// valid kind/TIF combinations), matches it against the opposite side of
/// its own OrderBook, emits Trades via its observers for whatever
/// executes, and - only for a LimitOrder+GTC with an unfilled remainder -
/// deposits that remainder into OrderBook to rest. One MatchingEngine per
/// asset (not natively multi-asset). The single std::visit over the Order
/// variant lives here, in submit() - nowhere else in core touches the
/// variant directly (see the discussion in the architecture notes on why
/// that keeps a future new order kind a one-function change).

#include <cstdint>
#include <functional>
#include <variant>
#include <vector>

#include <core/concepts.hpp>
#include <core/order.hpp>
#include <core/order_book.hpp>
#include <core/order_id.hpp>
#include <core/side.hpp>
#include <core/trade.hpp>

namespace mms::core {

/// True if a Limit order on incoming_side at incoming_price would trade
/// against a resting order at resting_price: Buy crosses a Sell resting at
/// or below its price; Sell crosses a Buy resting at or above its price.
template<OrderedArithmetic Price>
[[nodiscard]] bool
crosses(Side incoming_side, const Price& incoming_price, const Price& resting_price) noexcept {
    return incoming_side == Side::Buy ? !(resting_price > incoming_price)
                                      : !(resting_price < incoming_price);
}

template<OrderedArithmetic Price, OrderedArithmetic Quantity> struct MatchResult {
    std::vector<Trade<Price, Quantity>> trades;
    Quantity remaining;
};

/// Walks the side opposite aggressor_side on `book`, from its best price
/// outward, matching while accepts(price) holds, consuming up to
/// `remaining` in total. Calls book.fill() for each match (mutates book)
/// and returns every Trade generated plus the unfilled remainder (zero if
/// fully filled). Does NOT notify anything - reporting the trades is the
/// caller's job, which is exactly what keeps this function testable with
/// nothing more than an OrderBook.
template<OrderedArithmetic Price, OrderedArithmetic Quantity, class Predicate>
[[nodiscard]] MatchResult<Price, Quantity> match_against_book(OrderBook<Price, Quantity>& book,
                                                              OrderId aggressor_id,
                                                              Side aggressor_side,
                                                              Quantity remaining,
                                                              Predicate accepts) {
    MatchResult<Price, Quantity> result{{}, remaining};
    const Side resting_side = (aggressor_side == Side::Buy) ? Side::Sell : Side::Buy;

    while (result.remaining > Quantity{} && !book.empty(resting_side)) {
        const auto top = book.best(resting_side);
        if (!accepts(top->order.price)) {
            break;
        }

        const Quantity traded =
            (result.remaining < top->order.quantity) ? result.remaining : top->order.quantity;
        book.fill(top->id, traded);

        Trade<Price, Quantity> trade{};
        trade.price = top->order.price;
        trade.quantity = traded;

        if (aggressor_side == Side::Buy) {
            trade.buy_order_id = aggressor_id;
            trade.sell_order_id = top->id;
        } else {
            trade.buy_order_id = top->id;
            trade.sell_order_id = aggressor_id;
        }

        result.trades.push_back(trade);

        result.remaining = result.remaining - traded;
    }
    return result;
}

/// Multiple virtual methods with no-op defaults, not a single on_event
/// entry point.
template<OrderedArithmetic Price, OrderedArithmetic Quantity> class MatchingEngineObserver {
public:
    virtual ~MatchingEngineObserver() = default;

    virtual void on_order_added(OrderId /*id*/, const LimitOrder<Price, Quantity>& /*order*/) {}

    virtual void on_order_cancelled(OrderId /*id*/) {}

    virtual void on_trade(const Trade<Price, Quantity>& /*trade*/) {}
};

template<OrderedArithmetic Price, OrderedArithmetic Quantity> class MatchingEngine {
public:
    /// Submits a new order (Limit or Market, any valid TIF) and returns the
    /// OrderId assigned to it - useful to later cancel() it if it ends up
    /// resting. An IOC/FOK that fills nothing, or a rejected FOK, still
    /// gets an id, even though nothing observable happens for it beyond
    /// whatever Trades were emitted.
    OrderId submit(const Order<Price, Quantity>& order) {
        return std::visit([this](const auto& concrete_order) { return submit_one(concrete_order); },
                          order);
    }

    /// Cancels a resting order. Returns false if id isn't currently
    /// resting (never existed, already filled, or already cancelled).
    bool cancel(OrderId id) {
        const bool cancelled = book_.cancel(id);
        if (cancelled) {
            notify_order_cancelled(id);
        }
        return cancelled;
    }

    void add_observer(MatchingEngineObserver<Price, Quantity>& observer) {
        observers_.push_back(observer);
    }

    [[nodiscard]] const OrderBook<Price, Quantity>& book() const noexcept { return book_; }

private:
    [[nodiscard]] static Side opposite(Side side) noexcept {
        return side == Side::Buy ? Side::Sell : Side::Buy;
    }

    void notify_order_added(OrderId id, const LimitOrder<Price, Quantity>& order) {
        for (auto& observer : observers_) {
            observer.get().on_order_added(id, order);
        }
    }

    void notify_order_cancelled(OrderId id) {
        for (auto& observer : observers_) {
            observer.get().on_order_cancelled(id);
        }
    }

    void notify_trades(const std::vector<Trade<Price, Quantity>>& trades) {
        for (auto& trade : trades) {
            for (auto& observer : observers_) {
                observer.get().on_trade(trade);
            }
        }
    }

    OrderId submit_one(const LimitOrder<Price, Quantity>& order) {
        const OrderId id = next_order_id();
        const auto accepts = [side = order.side, price = order.price](const Price& resting_price) {
            return crosses(side, price, resting_price);
        };

        if (order.time_in_force == TimeInForce::FOK &&
            book_.total_matchable(opposite(order.side), accepts) < order.quantity) {
            return id; // rejected: not fully fillable now, zero fills, nothing rests
        }

        auto result = match_against_book(book_, id, order.side, order.quantity, accepts);
        notify_trades(result.trades);

        if (result.remaining > Quantity{} && order.time_in_force == TimeInForce::GTC) {
            LimitOrder<Price, Quantity> resting = order;
            resting.quantity = result.remaining;
            book_.add(id, resting);
            notify_order_added(id, resting);
        }

        // IOC (or an unfilled FOK remainder, which cannot happen once past
        // the check above): the remainder is simply discarded, never rests.
        return id;
    }

    OrderId submit_one(const MarketOrder<Quantity>& order) {
        const OrderId id = next_order_id();
        const auto accepts = [](const Price&) { return true; }; // no price limit

        if (order.time_in_force == MarketTimeInForce::FOK &&
            book_.total_matchable(opposite(order.side), accepts) < order.quantity) {
            return id; // rejected
        }

        auto result = match_against_book(book_, id, order.side, order.quantity, accepts);
        notify_trades(result.trades);
        return id; // Market never rests, any remainder is discarded
    }

    [[nodiscard]] OrderId next_order_id() noexcept { return static_cast<OrderId>(next_id_++); }

    OrderBook<Price, Quantity> book_;
    std::vector<std::reference_wrapper<MatchingEngineObserver<Price, Quantity>>> observers_;
    std::uint64_t next_id_ = 1;
};

} // namespace mms::core

#endif // MMS_CORE_MATCHING_ENGINE_HPP