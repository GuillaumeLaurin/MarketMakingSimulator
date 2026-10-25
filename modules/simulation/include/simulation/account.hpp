#ifndef MMS_SIMULATION_ACCOUNT_HPP
#define MMS_SIMULATION_ACCOUNT_HPP

/// \file
/// Account: what one agent owns and what it has promised. Cash, a P&L ledger for
/// its positions, and a ticket for every order it has had accepted that has not
/// finished yet, each holding back the cash or the collateral that order may need.
/// The account does not know the matching engine or order ids; the loop ties a
/// ticket to an engine id when the order arrives (see Ticket).
///
/// check() applies the rules of the exchange to an action and says why it is not
/// acceptable, or nothing. accept() books a ticket and does not check, so the
/// caller checks first. on_fill() and release() close a ticket's life.
///
/// Nothing is cached: reserved cash, pending quantities and collateral are summed
/// over the tickets when asked, in ticket order. There are few tickets per agent,
/// and it keeps the totals exact and the iteration deterministic.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <agents/action.hpp>
#include <agents/reject_reason.hpp>
#include <mm/pnl.hpp>
#include <mm/types.hpp>

#include <simulation/account_rules.hpp>
#include <simulation/agent_profile.hpp>
#include <simulation/simulation_export.hpp>

namespace mms::simulation {

/// One accepted order that has not finished. Issued by Account::accept, in
/// increasing order, and never reused.
enum class Ticket : std::uint64_t {};

class MMS_SIMULATION_EXPORT Account {
public:
    /// `assets` is the same list the world gives its ledger: one entry per asset,
    /// in AssetId order. Throws std::invalid_argument for an empty name, an
    /// initial_cash that is not finite and >= 0, rules that are not finite and
    /// >= 0, a negative max_abs_position, and anything PnlLedger rejects.
    Account(AgentProfile profile, AccountRules rules, const std::vector<mm::LedgerAsset>& assets);

    [[nodiscard]] const AgentProfile& profile() const noexcept { return profile_; }

    [[nodiscard]] const AccountRules& rules() const noexcept { return rules_; }

    /// Positions and P&L. Its last mid of each asset is the price everything below
    /// is valued at.
    [[nodiscard]] const mm::PnlLedger& ledger() const noexcept { return ledger_; }

    /// What the agent holds, before anything is held back. Starts at initial_cash:
    /// a buy lowers it by price times quantity, a sale raises it, fees come off.
    /// It can go negative (a covering buy after a big rise, or a fee), and nothing
    /// forces the account closed.
    [[nodiscard]] double cash() const noexcept { return cash_; }

    /// Cash held back by pending buys: for each, its reserve price times what is
    /// left of it.
    [[nodiscard]] double reserved_cash() const;

    /// Collateral held back for short positions, at the last mid:
    /// short_collateral_rate times, for each asset, the most the agent could be
    /// short once its pending sales fill, which is those sales minus the stock it
    /// holds, never below zero. Pending buys do not reduce it.
    [[nodiscard]] double collateral() const;

    /// cash() - reserved_cash() - collateral(). What the agent may still commit to
    /// new risk. Can be negative, which blocks every order that adds risk and none
    /// that reduces it.
    [[nodiscard]] double buying_power() const;

    /// Quantity still to fill in the pending buys / sales of `asset`.
    [[nodiscard]] mm::Quantity pending_buy(mm::AssetId asset) const;
    [[nodiscard]] mm::Quantity pending_sell(mm::AssetId asset) const;

    /// How many tickets are open.
    [[nodiscard]] std::size_t pending_count() const noexcept { return pending_.size(); }

    /// Why the action cannot be accepted, or nothing. Changes nothing. In this
    /// order:
    ///   UnknownAsset      the asset does not exist
    ///   InvalidOrder      a quantity <= 0, or a limit price not finite and > 0
    ///   PositionLimit     past max_abs_position once the pending orders of that
    ///                     side fill, unless the order brings |position| down
    ///   ShortNotAllowed   it would sell more than the stock not already promised
    ///                     to pending sales, and the agent may not short
    ///   InsufficientCollateral  that short would take buying power below zero
    ///   InsufficientCash  the part of a buy that does not cover a short would
    ///                     take buying power below zero; a market buy is valued at
    ///                     the last mid times (1 + market_order_buffer)
    /// A part that only reduces a position, a sale of stock held or a purchase that
    /// covers a short, never needs cash or collateral. A Cancel is always
    /// acceptable here: whether the order exists is for the loop to say.
    [[nodiscard]] std::optional<agents::RejectReason> check(const agents::Action& action) const;

    /// Books the order and holds back what it needs: the limit price (or, for a
    /// market buy, the estimate above) times the quantity for a buy. Does not
    /// check. Throws std::out_of_range for an unknown asset.
    [[nodiscard]] Ticket accept(const agents::SubmitLimit& order);
    [[nodiscard]] Ticket accept(const agents::SubmitMarket& order);

    /// `quantity` of the ticket's order traded at `price`. Moves cash, updates
    /// the ledger at the last mid, and shrinks the ticket; a ticket with nothing
    /// left closes by itself. Throws std::out_of_range for an unknown ticket and
    /// std::invalid_argument for a quantity that is <= 0 or more than what is left.
    void on_fill(Ticket ticket, mm::Price price, mm::Quantity quantity);

    /// The order ended without trading what is left of it: it was cancelled, or it
    /// was an immediate-or-cancel / fill-or-kill that did not rest. Frees what the
    /// ticket held back. Returns false for a ticket that is not open (it already
    /// closed), which is not an error.
    bool release(Ticket ticket);

    /// The mid of `asset` moved: marks the ledger, and with it the collateral.
    void on_mark(mm::AssetId asset, mm::Price mid) { ledger_.on_mark(asset, mid); }

private:
    struct Pending {
        mm::AssetId asset;
        mm::Side side;
        mm::Price reserved_price; ///< what a buy is valued at; unused for a sale
        mm::Quantity remaining;
    };

    [[nodiscard]] Ticket open(const Pending& pending);

    AgentProfile profile_;
    AccountRules rules_;
    mm::PnlLedger ledger_;
    double cash_;
    std::map<Ticket, Pending> pending_;
    std::uint64_t next_ticket_ = 1;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_ACCOUNT_HPP