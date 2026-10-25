/// \file
/// Tests for simulation/account.hpp. The account is checked from the outside: what
/// check() says for each kind of action and in which order the reasons come, what a
/// ticket holds back, how a fill moves cash, positions and the reservation, and how
/// the buying power moves with the mid. Money is kept simple (a mid of 100, whole
/// share counts) so that every figure can be worked out by hand.

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <agents/action.hpp>
#include <agents/reject_reason.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/pnl.hpp>
#include <mm/types.hpp>

#include <simulation/account.hpp>
#include <simulation/account_rules.hpp>
#include <simulation/agent_profile.hpp>

namespace {

namespace sim = mms::simulation;
namespace agents = mms::agents;
namespace mm = mms::mm;

using agents::RejectReason;
using Catch::Approx;
using mm::Side;

constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();

std::vector<mm::LedgerAsset> assets(std::size_t count = 2, double fee_bps = 0.0) {
    return std::vector<mm::LedgerAsset>(
        count, mm::LedgerAsset{.initial_mid = 100.0, .maker_fee_bps = fee_bps});
}

sim::AgentProfile profile(double cash = 10000.0,
                          bool can_short = false,
                          std::optional<mm::Quantity> max_abs_position = std::nullopt) {
    return sim::AgentProfile{.name = "agent",
                             .initial_cash = cash,
                             .can_short = can_short,
                             .max_abs_position = max_abs_position};
}

sim::Account make(sim::AgentProfile p = profile(),
                  sim::AccountRules rules = {},
                  std::vector<mm::LedgerAsset> a = assets()) {
    return sim::Account(std::move(p), rules, a);
}

agents::SubmitLimit limit(Side side, double price, mm::Quantity quantity, mm::AssetId asset = 0) {
    return agents::SubmitLimit{.asset = asset, .side = side, .price = price, .quantity = quantity};
}

agents::SubmitMarket market(Side side, mm::Quantity quantity, mm::AssetId asset = 0) {
    return agents::SubmitMarket{.asset = asset, .side = side, .quantity = quantity};
}

std::optional<RejectReason> check(const sim::Account& account, const agents::Action& action) {
    return account.check(action);
}

// Opens a position by accepting and filling an order, bypassing check().
void take_position(
    sim::Account& account, Side side, double price, mm::Quantity quantity, mm::AssetId asset = 0) {
    const sim::Ticket ticket = account.accept(limit(side, price, quantity, asset));
    account.on_fill(ticket, price, quantity);
}

} // namespace

// ----------------------------------------------------------------- construction

TEST_CASE("A new account holds its initial cash and nothing else", "[simulation][account]") {
    const sim::Account account = make(profile(2500.0));

    CHECK(account.cash() == 2500.0);
    CHECK(account.reserved_cash() == 0.0);
    CHECK(account.collateral() == 0.0);
    CHECK(account.buying_power() == 2500.0);
    CHECK(account.pending_count() == 0);
    CHECK(account.pending_buy(0) == 0);
    CHECK(account.pending_sell(1) == 0);
    CHECK(account.ledger().asset_count() == 2);
    CHECK(account.ledger().position(0).quantity() == 0);
    CHECK(account.profile().name == "agent");
    CHECK(account.rules().short_collateral_rate == 1.0);
}

TEST_CASE("The account refuses a profile or rules that make no sense", "[simulation][account]") {
    sim::AgentProfile nameless = profile();
    nameless.name = "";
    CHECK_THROWS_AS(make(nameless), std::invalid_argument);
    CHECK_THROWS_AS(make(profile(-1.0)), std::invalid_argument);
    CHECK_THROWS_AS(make(profile(not_a_number)), std::invalid_argument);
    CHECK_THROWS_AS(make(profile(infinity)), std::invalid_argument);
    CHECK_THROWS_AS(make(profile(100.0, false, -1)), std::invalid_argument);

    CHECK_THROWS_AS(make(profile(), sim::AccountRules{.short_collateral_rate = -0.1}),
                    std::invalid_argument);
    CHECK_THROWS_AS(make(profile(), sim::AccountRules{.short_collateral_rate = not_a_number}),
                    std::invalid_argument);
    CHECK_THROWS_AS(make(profile(), sim::AccountRules{.market_order_buffer = -0.1}),
                    std::invalid_argument);
    CHECK_THROWS_AS(make(profile(), sim::AccountRules{.market_order_buffer = infinity}),
                    std::invalid_argument);

    CHECK_THROWS_AS(make(profile(), {}, {}), std::invalid_argument); // the ledger needs an asset
}

TEST_CASE("Zero cash, a zero limit and zero rules are legitimate", "[simulation][account]") {
    CHECK_NOTHROW(make(profile(0.0)));
    CHECK_NOTHROW(make(profile(100.0, false, 0)));
    CHECK_NOTHROW(make(
        profile(), sim::AccountRules{.short_collateral_rate = 0.0, .market_order_buffer = 0.0}));
}

// ----------------------------------------------------------------------- check()

TEST_CASE("An unknown asset is refused, whatever the kind of order",
          "[simulation][account][check]") {
    const sim::Account account = make();

    CHECK(check(account, limit(Side::Buy, 100.0, 1, 2)) == RejectReason::UnknownAsset);
    CHECK(check(account, limit(Side::Sell, 100.0, 1, 99)) == RejectReason::UnknownAsset);
    CHECK(check(account, market(Side::Buy, 1, 2)) == RejectReason::UnknownAsset);
    CHECK(check(account, market(Side::Sell, 1, 2)) == RejectReason::UnknownAsset);
}

TEST_CASE("A cancel is always acceptable here, even for an unknown asset",
          "[simulation][account][check]") {
    const sim::Account account = make(profile(0.0));

    CHECK_FALSE(check(account, agents::Cancel{.asset = 0, .id = mm::OrderId{1}}).has_value());
    CHECK_FALSE(check(account, agents::Cancel{.asset = 50, .id = mm::OrderId{1}}).has_value());
}

TEST_CASE("A quantity that is not positive, or a limit price that is not, is invalid",
          "[simulation][account][check]") {
    const sim::Account account = make();

    CHECK(check(account, limit(Side::Buy, 100.0, 0)) == RejectReason::InvalidOrder);
    CHECK(check(account, limit(Side::Buy, 100.0, -5)) == RejectReason::InvalidOrder);
    CHECK(check(account, limit(Side::Sell, 100.0, 0)) == RejectReason::InvalidOrder);
    CHECK(check(account, market(Side::Buy, 0)) == RejectReason::InvalidOrder);
    CHECK(check(account, market(Side::Sell, -1)) == RejectReason::InvalidOrder);
    for (const double price : {0.0, -1.0, not_a_number, infinity}) {
        CHECK(check(account, limit(Side::Buy, price, 1)) == RejectReason::InvalidOrder);
        CHECK(check(account, limit(Side::Sell, price, 1)) == RejectReason::InvalidOrder);
    }
}

TEST_CASE("An unknown asset is reported before an invalid order", "[simulation][account][check]") {
    const sim::Account account = make();

    CHECK(check(account, limit(Side::Buy, -1.0, 0, 7)) == RejectReason::UnknownAsset);
}

TEST_CASE("An invalid order is reported before a lack of cash", "[simulation][account][check]") {
    const sim::Account account = make(profile(0.0));

    CHECK(check(account, limit(Side::Buy, 100.0, 0)) == RejectReason::InvalidOrder);
}

TEST_CASE("A buy is accepted up to the cash and refused beyond it",
          "[simulation][account][check]") {
    const sim::Account account = make(profile(10000.0));

    CHECK_FALSE(check(account, limit(Side::Buy, 100.0, 100)).has_value()); // exactly the cash
    CHECK(check(account, limit(Side::Buy, 100.0, 101)) == RejectReason::InsufficientCash);
    CHECK(check(account, limit(Side::Buy, 100.01, 100)) == RejectReason::InsufficientCash);
}

TEST_CASE("A market buy is valued at the last mid plus the buffer",
          "[simulation][account][check]") {
    const sim::Account account = make(profile(1000.0)); // buffer 5 %: 105 a share

    CHECK_FALSE(check(account, market(Side::Buy, 9)).has_value());                  // 945
    CHECK(check(account, market(Side::Buy, 10)) == RejectReason::InsufficientCash); // 1050

    const sim::Account no_buffer =
        make(profile(1000.0), sim::AccountRules{.market_order_buffer = 0.0});
    CHECK_FALSE(check(no_buffer, market(Side::Buy, 10)).has_value()); // 1000
}

TEST_CASE("A market buy follows the mid when it moves", "[simulation][account][check]") {
    sim::Account account = make(profile(1000.0), sim::AccountRules{.market_order_buffer = 0.0});

    account.on_mark(0, 200.0);

    CHECK_FALSE(check(account, market(Side::Buy, 5)).has_value());
    CHECK(check(account, market(Side::Buy, 6)) == RejectReason::InsufficientCash);
}

TEST_CASE("Cash held back by pending buys is not available twice", "[simulation][account][check]") {
    sim::Account account = make(profile(10000.0));
    static_cast<void>(account.accept(limit(Side::Buy, 100.0, 60)));

    CHECK_FALSE(check(account, limit(Side::Buy, 100.0, 40)).has_value());
    CHECK(check(account, limit(Side::Buy, 100.0, 41)) == RejectReason::InsufficientCash);
}

TEST_CASE("Selling stock that is held needs neither permission nor collateral",
          "[simulation][account][check]") {
    sim::Account account = make(profile(10000.0, false));
    take_position(account, Side::Buy, 100.0, 10);

    CHECK_FALSE(check(account, limit(Side::Sell, 100.0, 10)).has_value());
    CHECK_FALSE(check(account, market(Side::Sell, 4)).has_value());
}

TEST_CASE("Selling more than is held is a short sale, which needs permission",
          "[simulation][account][check]") {
    sim::Account account = make(profile(10000.0, false));
    take_position(account, Side::Buy, 100.0, 10);

    CHECK(check(account, limit(Side::Sell, 100.0, 11)) == RejectReason::ShortNotAllowed);
    CHECK(check(account, limit(Side::Sell, 100.0, 1, 1)) ==
          RejectReason::ShortNotAllowed); // nothing on asset 1
}

TEST_CASE("Stock promised to a pending sale cannot be sold again", "[simulation][account][check]") {
    sim::Account account = make(profile(10000.0, false));
    take_position(account, Side::Buy, 100.0, 10);
    static_cast<void>(account.accept(limit(Side::Sell, 100.0, 10)));

    CHECK(check(account, limit(Side::Sell, 100.0, 1)) == RejectReason::ShortNotAllowed);
}

TEST_CASE("A short sale needs collateral worth the rate times its value",
          "[simulation][account][check]") {
    const sim::Account full = make(profile(1000.0, true));              // rate 1.0, mid 100
    CHECK_FALSE(check(full, limit(Side::Sell, 100.0, 10)).has_value()); // 1000
    CHECK(check(full, limit(Side::Sell, 100.0, 11)) == RejectReason::InsufficientCollateral);

    const sim::Account half =
        make(profile(1000.0, true), sim::AccountRules{.short_collateral_rate = 0.5});
    CHECK_FALSE(check(half, limit(Side::Sell, 100.0, 20)).has_value()); // 1000
    CHECK(check(half, limit(Side::Sell, 100.0, 21)) == RejectReason::InsufficientCollateral);

    const sim::Account none =
        make(profile(0.0, true), sim::AccountRules{.short_collateral_rate = 0.0});
    CHECK_FALSE(check(none, limit(Side::Sell, 100.0, 1000)).has_value());
}

TEST_CASE("A short sale is valued at the mid, not at its own limit price",
          "[simulation][account][check]") {
    const sim::Account account = make(profile(1000.0, true));

    // A sale at 1 still ties up the share's value at the mid, 100.
    CHECK(check(account, limit(Side::Sell, 1.0, 11)) == RejectReason::InsufficientCollateral);
    CHECK_FALSE(check(account, limit(Side::Sell, 1.0, 10)).has_value());
}

TEST_CASE("The part of a sale that covers the stock held needs no collateral",
          "[simulation][account][check]") {
    sim::Account account = make(profile(1000.0, true));
    take_position(account, Side::Buy, 100.0, 5); // cash 500 left, 5 held

    // 5 from the stock and 5 short: collateral for the 5 only, 500, against 500 of cash.
    CHECK_FALSE(check(account, limit(Side::Sell, 100.0, 10)).has_value());
    CHECK(check(account, limit(Side::Sell, 100.0, 11)) == RejectReason::InsufficientCollateral);
}

TEST_CASE("The position limit caps a buy, counting the buys still pending",
          "[simulation][account][check]") {
    sim::Account account = make(profile(1e9, false, 100));

    CHECK_FALSE(check(account, limit(Side::Buy, 1.0, 100)).has_value());
    CHECK(check(account, limit(Side::Buy, 1.0, 101)) == RejectReason::PositionLimit);

    static_cast<void>(account.accept(limit(Side::Buy, 1.0, 60)));
    CHECK_FALSE(check(account, limit(Side::Buy, 1.0, 40)).has_value());
    CHECK(check(account, limit(Side::Buy, 1.0, 41)) == RejectReason::PositionLimit);
}

TEST_CASE("The position limit caps a short sale too", "[simulation][account][check]") {
    const sim::Account account = make(profile(1e9, true, 10));

    CHECK_FALSE(check(account, limit(Side::Sell, 1.0, 10)).has_value());
    CHECK(check(account, limit(Side::Sell, 1.0, 11)) == RejectReason::PositionLimit);
}

TEST_CASE("An order that brings the position down is accepted even past the limit",
          "[simulation][account][check]") {
    sim::Account account = make(profile(1e9, false, 10));
    take_position(
        account, Side::Buy, 1.0, 50); // the account does not check fills: 50 held, limit 10

    CHECK_FALSE(check(account, limit(Side::Sell, 1.0, 5)).has_value());
    CHECK_FALSE(check(account, limit(Side::Sell, 1.0, 50)).has_value());
    CHECK(check(account, limit(Side::Buy, 1.0, 1)) == RejectReason::PositionLimit);
}

TEST_CASE("The reasons come in a fixed order", "[simulation][account][check]") {
    // The limit before the permission to short, the permission before the collateral.
    const sim::Account capped = make(profile(0.0, false, 10));
    CHECK(check(capped, limit(Side::Sell, 100.0, 11)) == RejectReason::PositionLimit);

    const sim::Account forbidden = make(profile(0.0, false));
    CHECK(check(forbidden, limit(Side::Sell, 100.0, 1)) == RejectReason::ShortNotAllowed);

    const sim::Account poor = make(profile(0.0, true));
    CHECK(check(poor, limit(Side::Sell, 100.0, 1)) == RejectReason::InsufficientCollateral);
}

TEST_CASE("Buying back a short position never needs cash", "[simulation][account][check]") {
    sim::Account account = make(profile(1000.0, true));
    take_position(account, Side::Sell, 100.0, 10); // cash 2000, short 10, collateral 1000
    CHECK(account.buying_power() == Approx(1000.0));

    account.on_mark(0, 300.0); // the price tripled: collateral 3000, buying power -1000
    REQUIRE(account.buying_power() == Approx(-1000.0));

    CHECK_FALSE(check(account, limit(Side::Buy, 300.0, 10)).has_value()); // the whole short
    CHECK_FALSE(check(account, limit(Side::Buy, 300.0, 3)).has_value());  // a part of it
    // Past the short, the rest opens a long, which does need cash.
    CHECK(check(account, limit(Side::Buy, 300.0, 11)) == RejectReason::InsufficientCash);
    CHECK(check(account, limit(Side::Buy, 300.0, 15)) == RejectReason::InsufficientCash);
    // And so does anything unrelated: a negative buying power blocks new risk.
    CHECK(check(account, limit(Side::Buy, 100.0, 1, 1)) == RejectReason::InsufficientCash);
}

TEST_CASE("check() changes nothing", "[simulation][account][check]") {
    sim::Account account = make(profile(1000.0, true));
    take_position(account, Side::Buy, 100.0, 4);
    const double cash = account.cash();

    static_cast<void>(check(account, limit(Side::Buy, 100.0, 3)));
    static_cast<void>(check(account, market(Side::Sell, 50)));

    CHECK(account.cash() == cash);
    CHECK(account.pending_count() == 0);
    CHECK(account.ledger().position(0).quantity() == 4);
}

// --------------------------------------------------------------------- accept()

TEST_CASE("A buy holds back its limit price times its quantity", "[simulation][account][accept]") {
    sim::Account account = make(profile(10000.0));

    const sim::Ticket ticket = account.accept(limit(Side::Buy, 99.5, 20));

    CHECK(account.pending_count() == 1);
    CHECK(account.pending_buy(0) == 20);
    CHECK(account.pending_sell(0) == 0);
    CHECK(account.reserved_cash() == Approx(1990.0));
    CHECK(account.buying_power() == Approx(8010.0));
    CHECK(account.cash() == 10000.0); // held back, not spent
    static_cast<void>(ticket);
}

TEST_CASE("A sale holds back no cash and, when it goes short, collateral",
          "[simulation][account][accept]") {
    sim::Account account = make(profile(10000.0, true));

    static_cast<void>(account.accept(limit(Side::Sell, 120.0, 10)));

    CHECK(account.pending_sell(0) == 10);
    CHECK(account.reserved_cash() == 0.0);
    CHECK(account.collateral() == Approx(1000.0)); // 10 shares at the mid, 100
    CHECK(account.buying_power() == Approx(9000.0));
}

TEST_CASE("Collateral covers only the part of pending sales that the stock does not",
          "[simulation][account][accept]") {
    sim::Account account = make(profile(10000.0, true));
    take_position(account, Side::Buy, 100.0, 4);
    static_cast<void>(account.accept(limit(Side::Sell, 100.0, 10)));

    CHECK(account.collateral() == Approx(600.0)); // 10 pending minus 4 held, at 100
}

TEST_CASE("Pending buys do not reduce the collateral", "[simulation][account][accept]") {
    sim::Account account = make(profile(10000.0, true));
    take_position(account, Side::Sell, 100.0, 10); // short 10
    const double before = account.collateral();

    static_cast<void>(account.accept(limit(Side::Buy, 100.0, 10)));

    CHECK(account.collateral() == Approx(before));
}

TEST_CASE("Several pending sales on one asset add up in the collateral",
          "[simulation][account][accept]") {
    sim::Account account = make(profile(10000.0, true));

    static_cast<void>(account.accept(limit(Side::Sell, 100.0, 4)));
    static_cast<void>(account.accept(limit(Side::Sell, 101.0, 6)));
    static_cast<void>(account.accept(limit(Side::Sell, 100.0, 5, 1))); // another asset

    CHECK(account.pending_sell(0) == 10);
    CHECK(account.collateral() == Approx(1500.0)); // 10 shares and 5 shares, at 100
}

TEST_CASE("Several pending sales are covered together by the stock held",
          "[simulation][account][accept]") {
    sim::Account account = make(profile(10000.0, true));
    take_position(account, Side::Buy, 100.0, 7);

    static_cast<void>(account.accept(limit(Side::Sell, 100.0, 4)));
    static_cast<void>(account.accept(limit(Side::Sell, 100.0, 6)));

    CHECK(account.collateral() == Approx(300.0)); // 10 pending minus 7 held, at 100
}

TEST_CASE("Collateral is the rate times the value, and follows the mid",
          "[simulation][account][accept]") {
    sim::Account account =
        make(profile(10000.0, true), sim::AccountRules{.short_collateral_rate = 0.5});
    take_position(account, Side::Sell, 100.0, 10);
    CHECK(account.collateral() == Approx(500.0));

    account.on_mark(0, 150.0);

    CHECK(account.collateral() == Approx(750.0));
    CHECK(account.buying_power() == Approx(account.cash() - 750.0));
}

TEST_CASE("A market buy holds back the mid plus the buffer, a market sale no cash",
          "[simulation][account][accept]") {
    sim::Account account = make(profile(10000.0));

    static_cast<void>(account.accept(market(Side::Buy, 10)));
    CHECK(account.reserved_cash() == Approx(1050.0)); // 105 a share

    static_cast<void>(account.accept(market(Side::Sell, 3)));
    CHECK(account.reserved_cash() == Approx(1050.0));
    CHECK(account.pending_sell(0) == 3);
}

TEST_CASE("Tickets are distinct and come in order", "[simulation][account][accept]") {
    sim::Account account = make();

    const sim::Ticket first = account.accept(limit(Side::Buy, 100.0, 1));
    const sim::Ticket second = account.accept(limit(Side::Buy, 100.0, 1));
    const sim::Ticket third = account.accept(market(Side::Sell, 1));

    CHECK(first == sim::Ticket{1});
    CHECK(second == sim::Ticket{2});
    CHECK(third == sim::Ticket{3});
}

TEST_CASE("accept() refuses an unknown asset or an empty quantity, and books nothing",
          "[simulation][account][accept]") {
    sim::Account account = make();

    CHECK_THROWS_AS(account.accept(limit(Side::Buy, 100.0, 1, 5)), std::out_of_range);
    CHECK_THROWS_AS(account.accept(market(Side::Buy, 1, 5)), std::out_of_range);
    CHECK_THROWS_AS(account.accept(limit(Side::Buy, 100.0, 0)), std::invalid_argument);
    CHECK_THROWS_AS(account.accept(market(Side::Sell, -2)), std::invalid_argument);

    CHECK(account.pending_count() == 0);
}

TEST_CASE("The pending quantities are per asset", "[simulation][account][accept]") {
    sim::Account account = make();
    static_cast<void>(account.accept(limit(Side::Buy, 100.0, 7, 0)));
    static_cast<void>(account.accept(limit(Side::Buy, 100.0, 3, 1)));
    static_cast<void>(account.accept(limit(Side::Sell, 100.0, 2, 1)));

    CHECK(account.pending_buy(0) == 7);
    CHECK(account.pending_buy(1) == 3);
    CHECK(account.pending_sell(0) == 0);
    CHECK(account.pending_sell(1) == 2);
    CHECK_THROWS_AS(account.pending_buy(2), std::out_of_range);
    CHECK_THROWS_AS(account.pending_sell(2), std::out_of_range);
}

// --------------------------------------------------------------------- on_fill()

TEST_CASE("A buy fill spends cash at the fill price and closes a finished ticket",
          "[simulation][account][fill]") {
    sim::Account account = make(profile(10000.0));
    const sim::Ticket ticket = account.accept(limit(Side::Buy, 101.0, 10));

    account.on_fill(ticket, 100.0, 10); // filled better than the limit

    CHECK(account.cash() == Approx(9000.0));
    CHECK(account.ledger().position(0).quantity() == 10);
    CHECK(account.pending_count() == 0);
    CHECK(account.reserved_cash() == 0.0);
    CHECK(account.buying_power() == Approx(9000.0));
}

TEST_CASE("A partial fill leaves the rest of the ticket and of its reservation",
          "[simulation][account][fill]") {
    sim::Account account = make(profile(10000.0));
    const sim::Ticket ticket = account.accept(limit(Side::Buy, 100.0, 10));

    account.on_fill(ticket, 100.0, 4);

    CHECK(account.pending_count() == 1);
    CHECK(account.pending_buy(0) == 6);
    CHECK(account.reserved_cash() == Approx(600.0));
    CHECK(account.cash() == Approx(9600.0));

    account.on_fill(ticket, 100.0, 6);
    CHECK(account.pending_count() == 0);
}

TEST_CASE("A sell fill brings cash in", "[simulation][account][fill]") {
    sim::Account account = make(profile(10000.0, true));
    const sim::Ticket ticket = account.accept(limit(Side::Sell, 110.0, 5));

    account.on_fill(ticket, 110.0, 5);

    CHECK(account.cash() == Approx(10550.0));
    CHECK(account.ledger().position(0).quantity() == -5);
}

TEST_CASE("Fees come off the cash, and a rebate goes in", "[simulation][account][fill]") {
    sim::Account paying = make(profile(10000.0), {}, assets(1, 10.0)); // 10 bp
    const sim::Ticket buy = paying.accept(limit(Side::Buy, 100.0, 10));
    paying.on_fill(buy, 100.0, 10);
    CHECK(paying.cash() == Approx(10000.0 - 1000.0 - 1.0));

    sim::Account rebated = make(profile(10000.0), {}, assets(1, -10.0));
    const sim::Ticket other = rebated.accept(limit(Side::Buy, 100.0, 10));
    rebated.on_fill(other, 100.0, 10);
    CHECK(rebated.cash() == Approx(10000.0 - 1000.0 + 1.0));
}

TEST_CASE("The ledger sees the fill at the last mid", "[simulation][account][fill]") {
    sim::Account account = make(profile(10000.0));
    account.on_mark(0, 100.5);
    const sim::Ticket ticket = account.accept(limit(Side::Buy, 100.0, 10));

    account.on_fill(ticket, 100.0, 10);

    CHECK(account.ledger().breakdown(0).spread_capture == Approx(5.0)); // (100.5 - 100) a share
}

TEST_CASE("A fill on an unknown ticket, or too large or empty, is refused",
          "[simulation][account][fill]") {
    sim::Account account = make(profile(10000.0));
    const sim::Ticket ticket = account.accept(limit(Side::Buy, 100.0, 10));

    CHECK_THROWS_AS(account.on_fill(sim::Ticket{99}, 100.0, 1), std::out_of_range);
    CHECK_THROWS_AS(account.on_fill(ticket, 100.0, 0), std::invalid_argument);
    CHECK_THROWS_AS(account.on_fill(ticket, 100.0, -1), std::invalid_argument);
    CHECK_THROWS_AS(account.on_fill(ticket, 100.0, 11), std::invalid_argument);

    CHECK(account.pending_buy(0) == 10);
    CHECK(account.cash() == 10000.0);
}

TEST_CASE("A fill the ledger rejects leaves the account as it was", "[simulation][account][fill]") {
    sim::Account account = make(profile(10000.0));
    const sim::Ticket ticket = account.accept(limit(Side::Buy, 100.0, 10));

    CHECK_THROWS_AS(account.on_fill(ticket, not_a_number, 5), std::invalid_argument);

    CHECK(account.cash() == 10000.0);
    CHECK(account.pending_buy(0) == 10);
    CHECK(account.ledger().position(0).quantity() == 0);
}

TEST_CASE("A closed ticket cannot be filled again", "[simulation][account][fill]") {
    sim::Account account = make(profile(10000.0));
    const sim::Ticket ticket = account.accept(limit(Side::Buy, 100.0, 1));
    account.on_fill(ticket, 100.0, 1);

    CHECK_THROWS_AS(account.on_fill(ticket, 100.0, 1), std::out_of_range);
}

// --------------------------------------------------------------------- release()

TEST_CASE("Releasing a ticket frees what it held back", "[simulation][account][release]") {
    sim::Account account = make(profile(10000.0, true));
    const sim::Ticket buy = account.accept(limit(Side::Buy, 100.0, 10));
    const sim::Ticket sell = account.accept(limit(Side::Sell, 100.0, 10));
    REQUIRE(account.buying_power() == Approx(10000.0 - 1000.0 - 1000.0));

    CHECK(account.release(buy));
    CHECK(account.buying_power() == Approx(10000.0 - 1000.0));
    CHECK(account.release(sell));
    CHECK(account.buying_power() == Approx(10000.0));
    CHECK(account.pending_count() == 0);
}

TEST_CASE("Releasing a ticket twice, or one that never existed, reports false",
          "[simulation][account][release]") {
    sim::Account account = make();
    const sim::Ticket ticket = account.accept(limit(Side::Buy, 100.0, 1));

    CHECK(account.release(ticket));
    CHECK_FALSE(account.release(ticket));
    CHECK_FALSE(account.release(sim::Ticket{500}));
}

TEST_CASE("Releasing after a full fill is not an error", "[simulation][account][release]") {
    sim::Account account = make();
    const sim::Ticket ticket = account.accept(limit(Side::Buy, 100.0, 1));
    account.on_fill(ticket, 100.0, 1);

    CHECK_FALSE(account.release(ticket));
}

TEST_CASE("Releasing after a partial fill frees only what is left",
          "[simulation][account][release]") {
    sim::Account account = make(profile(10000.0));
    const sim::Ticket ticket = account.accept(limit(Side::Buy, 100.0, 10));
    account.on_fill(ticket, 100.0, 4);

    CHECK(account.release(ticket));

    CHECK(account.reserved_cash() == 0.0);
    CHECK(account.cash() == Approx(9600.0)); // what was bought stays bought
    CHECK(account.ledger().position(0).quantity() == 4);
}

// ----------------------------------------------------------------------- on_mark

TEST_CASE("Marking an unknown asset is refused", "[simulation][account]") {
    sim::Account account = make();

    CHECK_THROWS_AS(account.on_mark(2, 100.0), std::out_of_range);
}
