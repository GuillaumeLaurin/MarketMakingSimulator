#ifndef MMS_MM_POSITION_HPP
#define MMS_MM_POSITION_HPP

/// \file
/// The position held on one asset, valued at average cost. The quantity is
/// signed (positive long, negative short: both are the ordinary state of a
/// market maker mid-flow, never an error). Only this instrument's own
/// accounting lives here: cash, fees and the mid price belong to the P&L
/// ledger, which owns one Position per asset.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include <mm/types.hpp>

namespace mms::mm {

class Position {
public:
    /// Signed number of shares held.
    [[nodiscard]] Quantity quantity() const noexcept { return quantity_; }

    /// Average price paid for the shares currently held. 0 when flat.
    [[nodiscard]] Price average_cost() const noexcept { return average_cost_; }

    /// Profit locked in so far by fills that reduced or reversed the position.
    ///  Fees are not included; they are a separate bucket in the ledger.
    [[nodiscard]] double realized_pnl() const noexcept { return realized_pnl_; }

    /// Profit that would be locked in by closing at `mark`. `mark` must be
    /// finite. 0 when flat.
    [[nodiscard]] double unrealized_pnl(Price mark) const noexcept {
        return quantity_ == 0 ? 0.0 : to_double(quantity_) * (mark - average_cost_);
    }

    /// Applies one fill and returns the P&L it realized (0 when it opens or
    /// adds to the position). Adding to the position moves the average cost to
    /// the volume-weighted mean. Reducing it leaves the average cost unchanged.
    /// A fill larger than the position closes it and opens the remainder on
    /// the other side at the fill price. Throws std::invalid_argument for a
    /// quantity <= 0 or a non-finite price.
    double on_fill(Side side, Price price, Quantity quantity) {
        if (quantity <= 0) {
            throw std::invalid_argument("position: fill quantity must be > 0");
        }
        if (!std::isfinite(price)) {
            throw std::invalid_argument("position: fill price must be finite");
        }

        const Quantity signed_fill = (side == Side::Buy) ? quantity : -quantity;
        const bool same_direction = (quantity_ > 0) == (signed_fill > 0);

        double realized = 0.0;
        if (quantity_ == 0) {
            average_cost_ = price;
        } else if (same_direction) {
            const double held = to_double(std::abs(quantity_));
            const double added = to_double(quantity);
            average_cost_ = ((held * average_cost_) + (added * price)) / (held + added);
        } else {
            const Quantity held = std::abs(quantity_);
            const double direction = (quantity_ > 0) ? 1.0 : -1.0;
            realized = direction * to_double(std::min(quantity, held)) * (price - average_cost_);
            if (quantity > held) {
                average_cost_ = price; // the remainder opens on the other side
            } else if (quantity == held) {
                average_cost_ = 0.0; // flat again
            }
        }

        quantity_ += signed_fill;
        realized_pnl_ += realized;
        return realized;
    }   


private:
    Quantity quantity_ = 0;
    Price average_cost_ = 0.0;
    double realized_pnl_ = 0.0;
};

} // namespace mms::mm

#endif // MMS_MM_POSITION_HPP