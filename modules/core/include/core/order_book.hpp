#ifndef MMS_CORE_ORDER_BOOK_HPP
#define MMS_CORE_ORDER_BOOK_HPP

/// \file
/// OrderBook: a PASSIVE container of resting LimitOrder entries (GTC only -
/// IOC/FOK/Market never rest, so they never enter this structure at all).
/// The public contract is deliberately narrow (add / cancel / fill / peek
/// the best entry / query depth) rather than exposing iteration over the
/// internal structure, so the underlying representation can be revisited
/// later by benchmark (see bench/) without changing this interface. This
/// implementation (std::map per price level + std::deque per level for
/// time priority, plus an OrderId -> (side, price) index) is a correct
/// reference implementation, not yet the benchmarked/optimized one.
/// MatchingEngine drives the actual matching algorithm by repeatedly
/// peeking best() and calling fill()/cancel() - OrderBook itself makes no
/// matching decisions.

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>

#include <core/concepts.hpp>
#include <core/order.hpp>
#include <core/order_id.hpp>
#include <core/side.hpp>

namespace mms::core {

/// A resting entry, returned by best() so callers get both the id (to
/// build a Trade) and the order data in one call.
template<OrderedArithmetic Price, OrderedArithmetic Quantity> struct BookEntry {
    OrderId id;
    LimitOrder<Price, Quantity> order;
};

/// One side's price levels: a price-ordered (ascending) map of FIFO
/// deques, each level holding the resting entries at that exact price in
/// time priority. No notion of Side at all - OrderBook decides whether
/// "best" means the lowest price (sell side) or the highest (buy side) by
/// choosing lowest()/best_lowest() vs highest()/best_highest(). Kept
/// separate from OrderBook so it's independently testable (add/remove/
/// reduce/best/depth without any Buy/Sell branching to reason about).
template<OrderedArithmetic Price, OrderedArithmetic Quantity> class PriceLevels {
public:
    using Level = std::deque<BookEntry<Price, Quantity>>;

    void add(OrderId id, const LimitOrder<Price, Quantity>& order) {
        levels_[order.price].push_back(BookEntry<Price, Quantity>{id, order});
        ++size_;
    }

    /// Removes the entry with this id at this price outright. Narrow
    /// contract: price must be an existing level containing id.
    void remove(const Price& price, OrderId id) {
        auto level_it = levels_.find(price);
        auto& level = level_it->second;
        level.erase(std::find_if(level.begin(),
                                 level.end(),
                                 [id](const BookEntry<Price, Quantity>& e) { return e.id == id; }));
        if (level.empty()) {
            levels_.erase(level_it);
        }

        --size_;
    }

    /// Reduces the entry's quantity by filled_quantity. Removes it if the
    /// remaining quantity reaches zero. Returns true if it was removed
    /// (fully filled), false if it's still resting. Narrow contract: price
    /// must exist and contain id, filled_quantity must not exceed its
    /// current quantity.
    bool reduce(const Price& price, OrderId id, const Quantity& filled_quantity) {
        auto level_it = levels_.find(price);
        auto& level = level_it->second;
        auto entry = std::find_if(level.begin(),
                                  level.end(),
                                  [id](const BookEntry<Price, Quantity>& e) { return e.id == id; });
        entry->order.quantity = entry->order.quantity - filled_quantity;
        if (entry->order.quantity > Quantity{}) {
            return false;
        }
        level.erase(entry);
        if (level.empty()) {
            levels_.erase(level_it);
        }
        --size_;
        return true;
    }

    /// Sums the quantity resting at prices satisfying `accepts`, walking from
    /// the lowest price outward, stopping at the first price that fails (the
    /// map is price-ascending, so once one fails, every higher price does too).
    /// Non-mutating - used to check Fill-Or-Kill feasibility before committing
    /// any fills.
    template<class Predicate>
    [[nodiscard]] Quantity total_matchable_ascending(Predicate accepts) const {
        Quantity total{};
        for (const auto& [price, level] : levels_) {
            if (!accepts(price)) {
                break;
            }
            for (const auto& entry : level) {
                total = total + entry.order.quantity;
            }
        }
        return total;
    }

    /// Same, walking from the highest price outward instead
    template<class Predicate>
    [[nodiscard]] Quantity total_matchable_descending(Predicate accepts) const {
        Quantity total{};
        for (auto it = levels_.rbegin(); it != levels_.rend(); ++it) {
            if (!accepts(it->first)) {
                break;
            }
            for (const auto& entry : it->second) {
                total = total + entry.order.quantity;
            }
        }
        return total;
    }

    [[nodiscard]] bool empty() const noexcept { return levels_.empty(); }

    [[nodiscard]] std::optional<BookEntry<Price, Quantity>> best_lowest() const {
        return levels_.empty()
                   ? std::nullopt
                   : std::optional<BookEntry<Price, Quantity>>(levels_.begin()->second.front());
    }

    [[nodiscard]] std::optional<BookEntry<Price, Quantity>> best_highest() const {
        return levels_.empty()
                   ? std::nullopt
                   : std::optional<BookEntry<Price, Quantity>>(levels_.rbegin()->second.front());
    }

    [[nodiscard]] std::optional<Price> lowest_price() const {
        return levels_.empty() ? std::nullopt : std::optional<Price>(levels_.begin()->first);
    }

    [[nodiscard]] std::optional<Price> highest_price() const {
        return levels_.empty() ? std::nullopt : std::optional<Price>(levels_.rbegin()->first);
    }

    [[nodiscard]] Quantity depth_at(const Price& price) const {
        Quantity total{};
        const auto found = levels_.find(price);
        if (found == levels_.end()) {
            return total;
        }
        for (const auto& entry : found->second) {
            total = total + entry.order.quantity;
        }
        return total;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    std::map<Price, Level> levels_;
    std::size_t size_ = 0;
};

template<OrderedArithmetic Price, OrderedArithmetic Quantity> class OrderBook {
public:
    void add(OrderId id, const LimitOrder<Price, Quantity>& order) {
        levels_for(order.side).add(id, order);
        index_.emplace(id, Location{order.side, order.price});
    }

    bool cancel(OrderId id) {
        const auto found = index_.find(id);
        if (found == index_.end()) {
            return false;
        }
        levels_for(found->second.side).remove(found->second.price, id);
        index_.erase(found);
        return true;
    }

    void fill(OrderId id, const Quantity& filled_quantity) {
        const auto found = index_.find(id);
        if (found == index_.end()) {
            return;
        }
        const bool removed =
            levels_for(found->second.side).reduce(found->second.price, id, filled_quantity);
        if (removed) {
            index_.erase(found);
        }
    }

    /// Total resting quantity on `side` matchable under `accepts` (a
    /// predicate on price), without mutating anything - used by MatchingEngine
    /// to check Fill-Or-Kill feasibility up front.
    template<class Predicate>
    [[nodiscard]] Quantity total_matchable(Side side, Predicate accepts) const {
        return side == Side::Buy ? levels_for(side).total_matchable_ascending(accepts)
                                 : levels_for(side).total_matchable_descending(accepts);
    }

    [[nodiscard]] bool empty(Side side) const noexcept { return levels_for(side).empty(); }

    [[nodiscard]] std::optional<BookEntry<Price, Quantity>> best(Side side) const {
        return side == Side::Buy ? levels_for(side).best_highest() : levels_for(side).best_lowest();
    }

    [[nodiscard]] std::optional<Price> best_price(Side side) const {
        return side == Side::Buy ? levels_for(side).highest_price()
                                 : levels_for(side).lowest_price();
    }

    [[nodiscard]] Quantity depth_at(Side side, const Price& price) const {
        return levels_for(side).depth_at(price);
    }

    [[nodiscard]] std::size_t size(Side side) const noexcept { return levels_for(side).size(); }

private:
    struct Location {
        Side side;
        Price price;
    };

    [[nodiscard]] PriceLevels<Price, Quantity>& levels_for(Side side) noexcept {
        return side == Side::Buy ? buy_levels_ : sell_levels_;
    }

    [[nodiscard]] const PriceLevels<Price, Quantity>& levels_for(Side side) const noexcept {
        return side == Side::Buy ? buy_levels_ : sell_levels_;
    }

    PriceLevels<Price, Quantity> buy_levels_;
    PriceLevels<Price, Quantity> sell_levels_;
    std::unordered_map<OrderId, Location> index_;
};

} // namespace mms::core

#endif // MMS_CORE_ORDER_BOOK_HPP