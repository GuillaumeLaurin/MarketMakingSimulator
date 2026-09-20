#ifndef MMS_MM_PNL_HPP
#define MMS_MM_PNL_HPP

/// \file
/// The P&L ledger: one Position per asset, plus everything needed to explain
/// the profit two ways. View one is accounting (realized, unrealized). View
/// two is economic (spread captured at the fill, then inventory P&L from the
/// mid moving while a position is held). Gross P&L is the same number in both
/// views; fees are a third bucket on top. simulation owns the ledger and
/// feeds it fills and mids; mm only defines it.

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <mm/position.hpp>
#include <mm/types.hpp>

namespace mms::mm {

/// The P&L of one asset or of the whole portfolio. Every field is a P&L
/// contribution: fees are negative when paid and positive for a rebate.
struct PnlBreakdown {
    double realized = 0.0;
    double unrealized = 0.0;
    double spread_capture = 0.0;
    double inventory = 0.0;
    double fees = 0.0;

    /// Profit before fees. Both views give the same figure:
    /// realized + unrealized == spread_capture + inventory.
    [[nodiscard]] double gross() const noexcept { return realized + unrealized; }

    [[nodiscard]] double total() const noexcept { return gross() + fees; }

    PnlBreakdown& operator+=(const PnlBreakdown& other) noexcept {
        realized += other.realized;
        unrealized += other.unrealized;
        spread_capture += other.spread_capture;
        inventory += other.inventory;
        fees += other.fees;
        return *this;
    }
};

/// What the ledger needs to know about an asset up front.
struct LedgerAsset {
    Price initial_mid;
    double maker_fee_bps = 0.0; ///< basis points of notional; negative is a rebate
};

class PnlLedger {
public:
    /// One entry per asset, indexed by AssetId. Throws std::invalid_argument
    /// for an empty list or a non-finite mid or fee rate.
    explicit PnlLedger(const std::vector<LedgerAsset>& assets) {
        if (assets.empty()) {
            throw std::invalid_argument("pnl: the ledger needs at least one asset");
        }
        assets_.reserve(assets.size());
        for (const LedgerAsset& asset : assets) {
            require_finite(asset.initial_mid, "initial mid");
            require_finite(asset.maker_fee_bps, "maker fee");
            assets_.push_back(AssetState{
                .position = Position{},
                .last_mid = asset.initial_mid,
                .maker_fee_bps = asset.maker_fee_bps
            });
        }
    }

    /// The mid of `asset` moved to `mid`: the position held since the last
    /// mark gains or loses q * (mid - last mid). Call it every tick so that
    /// unrealized P&L is current. Throws std::out_of_range for an unknown
    /// asset and std::invalid_argument for a non-finite mid.
    void on_mark(AssetId asset, Price mid) {
        AssetState& state = assets_.at(asset);
        require_finite(mid, "mid");
        accrue_inventory(state, mid);
    }

    /// A fill of `quantity` at `price`, when the mid was `mid`. The mid is
    /// applied first (inventory P&L up to this instant), then the fill: it
    /// books its spread capture, (mid - price) per share bought and
    /// (price - mid) per share sold, and its fee. Nothing is changed if any
    /// argument is rejected.
    void on_fill(AssetId asset, Side side, Price price, Quantity quantity, Price mid) {
        AssetState& state = assets_.at(asset);
        require_finite(mid, "mid");

        const Quantity held_before  = state.position.quantity();
        static_cast<void>(state.position.on_fill(side, price, quantity)); // validates price, quantity

        state.inventory += to_double(held_before) * (mid - state.last_mid);
        state.last_mid = mid;

        const double shares = to_double(quantity);
        state.spread_capture += ((side == Side::Buy) ? (mid - price) : (price - mid)) * shares;
        state.fees -= price * shares * state.maker_fee_bps * basis_point;
    }

    [[nodiscard]] std::size_t asset_count() const noexcept { return assets_.size(); }

    [[nodiscard]] const Position& position(AssetId asset) const {
        return assets_.at(asset).position;
    }

    [[nodiscard]] Price last_mid(AssetId asset) const { return assets_.at(asset).last_mid; }

    /// Signed inventory of every asset, in AssetId order.
    [[nodiscard]] std::vector<Quantity> quantities() const {
        std::vector<Quantity> result;
        result.reserve(assets_.size());
        for (const AssetState& state : assets_) {
            result.push_back(state.position.quantity());
        }
        return result;
    }

    /// The P&L of one asset, unrealized valued at its last mid.
    [[nodiscard]] PnlBreakdown breakdown(AssetId asset) const {
        const AssetState& state = assets_.at(asset);
        return PnlBreakdown{
            .realized = state.position.realized_pnl(),
            .unrealized = state.position.unrealized_pnl(state.last_mid),
            .spread_capture = state.spread_capture,
            .inventory = state.inventory,
            .fees = state.fees
        };
    }

    /// The P&L of the whole portfolio: the sum over every asset.
    [[nodiscard]] PnlBreakdown portfolio() const {
        PnlBreakdown sum;
        for (AssetId asset = 0; asset < assets_.size(); ++asset) {
            sum += breakdown(asset);
        }
        return sum;
    }


private:
    static constexpr double basis_point = 1.0e-4;

    struct AssetState {
        Position position;
        Price last_mid;
        double maker_fee_bps;
        double spread_capture = 0.0;
        double inventory = 0.0;
        double fees = 0.0;
    };

    static void require_finite(double value, const char* what) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument(std::string("pnl: ") + what + " must be finite");
        }
    }

    static void accrue_inventory(AssetState& state, Price mid) noexcept {
        state.inventory += to_double(state.position.quantity()) * (mid - state.last_mid);
        state.last_mid = mid;
    }

    std::vector<AssetState> assets_;
};

} // namespace mms::mm

#endif // MMS_MM_PNL_HPP