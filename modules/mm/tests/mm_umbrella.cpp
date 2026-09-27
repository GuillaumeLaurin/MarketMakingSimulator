/// \file
/// The <mm/mm.hpp> umbrella pulls in every public symbol of the mm module:
/// one small end-to-end scenario touching each header, from config through
/// a live quote to the P&L that fill produces.

#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mm/mm.hpp>

TEST_CASE("<mm/mm.hpp> exposes the whole mm module", "[mm][umbrella]") {
    using namespace mms::mm;
    namespace math = mms::math;

    // types.hpp
    static_assert(std::is_same_v<Price, double>);
    const AssetId asset = 0;

    // strategy_config.hpp, horizon.hpp, risk.hpp: build a one-asset strategy.
    const StrategyConfig config{
        .gamma = 2.0,
        .k = math::Vector::Constant(1, 1.0),
        .correlation = math::Matrix::Identity(1, 1),
        .estimators = {ConstantSpec{.sigma = 0.2}},
        .horizon = ConstantHorizon{.tau = 1.0},
        .quote_sizes = {10},
        .tick_sizes = {0.01},
        .limits = {.max_abs_position = {1000}, .max_portfolio_risk = std::nullopt},
        .min_samples = 0};

    // quoting_strategy.hpp: strategy_config.hpp returns the base interface.
    const std::unique_ptr<QuotingStrategy> strategy = make_strategy(config);
    strategy->on_market_data(math::Vector::Constant(1, 100.0), 1.0);

    // avellaneda_stoikov_strategy.hpp, quote.hpp: get a QuoteSet.
    const QuoteSet quotes =
        strategy->quote(make_market_view(math::Vector::Constant(1, 100.0), {Quantity{0}}, 0.0));
    REQUIRE(quotes.size() == 1);
    REQUIRE(quotes[0].bid.has_value());
    REQUIRE(quotes[0].ask.has_value());

    // requote.hpp: nothing resting yet, so the quote is planned as a submit.
    const RequotePlan plan =
        plan_requote(quotes, LiveQuoteSet(1), std::vector<Price>{0.01}, Ticks{1});
    CHECK(plan.cancels.empty());
    REQUIRE(plan.submits.size() == 2);

    // tick.hpp: the submitted bid sits on the tick grid.
    CHECK(nearest_ticks(plan.submits[0].quote.price, 0.01) ==
          bid_ticks(plan.submits[0].quote.price, 0.01));

    // position.hpp, pnl.hpp: a fill at the ask, marked at the same mid.
    PnlLedger ledger({LedgerAsset{.initial_mid = 100.0, .maker_fee_bps = 1.0}});
    ledger.on_fill(asset, Side::Buy, quotes[0].ask->price, quotes[0].ask->quantity, 100.0);

    CHECK(ledger.position(asset).quantity() == quotes[0].ask->quantity);
    CHECK(ledger.breakdown(asset).fees < 0.0);

    // risk.hpp: the ledger's own inventory feeds back into apply_limits.
    QuoteSet next = quotes;
    const math::Vector q = math::Vector::Constant(1, to_double(ledger.position(asset).quantity()));
    apply_limits(next, config.limits, q, math::Vector::Zero(1));
    CHECK(next[0].bid.has_value()); // far from the limit, nothing suppressed
}
