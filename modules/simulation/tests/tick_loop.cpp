/// \file
/// Tests for simulation/tick_loop.hpp. The scenarios are deliberately
/// controllable: the true price barely moves (volatility 1e-9) except for a
/// chosen drift, adverse flow is mostly INJECTED into the world's event queue
/// before a step (so a fill's size and instant are exact), and the market
/// maker's quotes are read back from loop.live_quotes() and from the books.
/// The properties that matter most are checked as invariants over many ticks
/// and seeds: what the loop believes it has live is exactly what rests in the
/// books, even with random latency, races between a cancel and a fill, and
/// the market maker crossing its own stale quote.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mm/quote.hpp>
#include <mm/requote.hpp>
#include <mm/strategy_config.hpp>
#include <mm/types.hpp>

#include <math/rng/seed.hpp>
#include <math/types.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/delay_spec.hpp>
#include <simulation/simulation_config.hpp>
#include <simulation/simulation_observer.hpp>
#include <simulation/tick_loop.hpp>
#include <simulation/world.hpp>

namespace sim = mms::simulation;
namespace math = mms::math;
namespace mm = mms::mm;

namespace {

struct Knobs {
    std::size_t assets = 1;
    double mu = 0.0;
    double lambda = 0.0;
    double correlation = 0.0;
    double volatility = 1.0e-9;
    double fee_bps = 0.0;
    sim::DelaySpec quote_delay = sim::ConstantDelaySpec{.delay = 0.0};
    sim::DelaySpec adverse_delay = sim::ConstantDelaySpec{.delay = 0.0};
    mm::Ticks threshold = 1;
    std::uint64_t seed = 1;
};

sim::AssetSpec asset_for(const Knobs& knobs, mm::Price mid) {
    return sim::AssetSpec{.symbol = "ASSET" + std::to_string(static_cast<int>(mid)),
                          .initial_mid = mid,
                          .maker_fee_bps = knobs.fee_bps,
                          .price_volatility = knobs.volatility,
                          .mu = knobs.mu,
                          .factor_model = std::nullopt,
                          .adverse_flow_lambda = knobs.lambda,
                          .adverse_flow_direction_correlation = knobs.correlation,
                          .adverse_flow_size = sim::UniformIntSpec{.min = 1, .max = 5}};
}

mm::StrategyConfig strategy_for(std::size_t assets) {
    const auto n = static_cast<Eigen::Index>(assets);
    return mm::StrategyConfig{
        .gamma = 2.0,
        .k = math::Vector::Constant(n, 1.0),
        .correlation = math::Matrix::Identity(n, n),
        .estimators = std::vector<mm::EstimatorSpec>(assets, mm::ConstantSpec{.sigma = 0.2}),
        .horizon = mm::ConstantHorizon{.tau = 1.0},
        .quote_sizes = std::vector<mm::Quantity>(assets, 10),
        .tick_sizes = std::vector<mm::Price>(assets, 0.01),
        .limits = {.max_abs_position = std::vector<mm::Quantity>(assets, 1000),
                   .max_portfolio_risk = std::nullopt},
        .min_samples = 0};
}

sim::SimulationConfig make_config(const Knobs& knobs = {}) {
    std::vector<sim::AssetSpec> assets;
    for (std::size_t i = 0; i < knobs.assets; ++i) {
        assets.push_back(asset_for(knobs, 100.0 + (50.0 * static_cast<double>(i))));
    }
    const auto n = static_cast<Eigen::Index>(knobs.assets);
    return sim::SimulationConfig{
        .assets = std::move(assets),
        .correlation = math::Matrix::Identity(n, n),
        .process = sim::RandomWalkSpec{},
        .strategy = strategy_for(knobs.assets),
        .latency = {.quote_delay = knobs.quote_delay, .adverse_flow_delay = knobs.adverse_delay},
        .requote_threshold_ticks = knobs.threshold,
        .master_seed = math::rng::Seed{knobs.seed},
        .dt = 1.0,
        .tick_count = 100};
}

/// Records everything a run notifies, in order.
struct Recorder final : sim::SimulationObserver {
    struct TradeSeen {
        mm::AssetId asset;
        mm::Trade trade;
        bool aggressor_is_mm;
    };

    std::vector<std::string> sequence;
    std::vector<double> starts;
    std::vector<double> ends;
    std::vector<std::pair<mm::AssetId, mm::Price>> marks;
    std::vector<std::pair<mm::AssetId, mm::AssetQuotes>> quotes;
    std::vector<TradeSeen> trades;

    void on_tick_start(double time) override {
        sequence.emplace_back("start");
        starts.push_back(time);
    }

    void on_mark(mm::AssetId asset, mm::Price mid) override {
        sequence.emplace_back("mark");
        marks.emplace_back(asset, mid);
    }

    void on_quote(mm::AssetId asset, const mm::AssetQuotes& asset_quotes) override {
        sequence.emplace_back("quote");
        quotes.emplace_back(asset, asset_quotes);
    }

    void on_trade(mm::AssetId asset, const mm::Trade& trade, bool aggressor_is_mm) override {
        sequence.emplace_back("trade");
        trades.push_back({.asset = asset, .trade = trade, .aggressor_is_mm = aggressor_is_mm});
    }

    void on_tick_end(double time) override {
        sequence.emplace_back("end");
        ends.push_back(time);
    }
};

/// Puts one adverse Market IOC order into the event queue, due immediately:
/// the next step executes it, before anything the loop itself schedules.
void inject_adverse(sim::World& world, mm::AssetId asset, mm::Side side, mm::Quantity quantity) {
    world.event_queue().schedule(
        0.0,
        sim::AdverseOrder{.asset = asset,
                          .order = mm::MarketOrder{.side = side,
                                                   .quantity = quantity,
                                                   .time_in_force = mm::MarketTimeInForce::IOC}});
}

/// The loop's belief about what is live must be exactly what rests in the books.
void check_consistent(const sim::World& world, const sim::TickLoop& loop) {
    for (mm::AssetId asset = 0; asset < world.asset_count(); ++asset) {
        const mm::OrderBook& book = world.matching_engine(asset).book();
        const mm::AssetLiveQuotes& live = loop.live_quotes()[asset];
        for (const mm::Side side : {mm::Side::Buy, mm::Side::Sell}) {
            const std::optional<mm::LiveQuote>& quote = side == mm::Side::Buy ? live.bid : live.ask;
            if (quote.has_value()) {
                CHECK(book.size(side) == 1);
                CHECK(book.depth_at(side, quote->quote.price) == quote->quote.quantity);
            } else {
                CHECK(book.size(side) == 0);
            }
        }
    }
}

mm::Price mid_of(const sim::World& world, mm::AssetId asset = 0) {
    return world.price_process().prices()(static_cast<Eigen::Index>(asset));
}

} // namespace

// ------------------------------------------------------------ construction

TEST_CASE("A fresh loop is at time zero with nothing live", "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config({.assets = 2});
    sim::World world(config);
    const sim::TickLoop loop(world, config);

    CHECK(loop.time() == 0.0);
    CHECK(loop.tick_count() == 0);
    REQUIRE(loop.live_quotes().size() == 2);
    for (const mm::AssetLiveQuotes& live : loop.live_quotes()) {
        CHECK_FALSE(live.bid.has_value());
        CHECK_FALSE(live.ask.has_value());
    }
}

TEST_CASE("The constructor rejects a time step that is not finite and positive",
          "[simulation][tick_loop]") {
    for (const double bad : {0.0, -1.0, std::nan(""), HUGE_VAL}) {
        sim::SimulationConfig config = make_config();
        sim::World world(config);
        config.dt = bad;

        CHECK_THROWS_AS(sim::TickLoop(world, config), std::invalid_argument);
    }
}

TEST_CASE("The constructor rejects a negative requote threshold", "[simulation][tick_loop]") {
    sim::SimulationConfig config = make_config();
    sim::World world(config);
    config.requote_threshold_ticks = -1;

    CHECK_THROWS_AS(sim::TickLoop(world, config), std::invalid_argument);
}

TEST_CASE("The constructor rejects an invalid latency spec", "[simulation][tick_loop]") {
    sim::SimulationConfig bad_quote = make_config();
    sim::World world_a(bad_quote);
    bad_quote.latency.quote_delay = sim::ConstantDelaySpec{.delay = -1.0};
    CHECK_THROWS_AS(sim::TickLoop(world_a, bad_quote), std::invalid_argument);

    sim::SimulationConfig bad_adverse = make_config();
    sim::World world_b(bad_adverse);
    bad_adverse.latency.adverse_flow_delay = sim::ExponentialDelaySpec{.mean = 0.0};
    CHECK_THROWS_AS(sim::TickLoop(world_b, bad_adverse), std::invalid_argument);
}

TEST_CASE("The constructor rejects an invalid adverse-flow spec, at construction not mid-run",
          "[simulation][tick_loop]") {
    // Even at a zero rate: the spec is validated up front.
    sim::SimulationConfig config = make_config();
    sim::World world(config);
    config.assets[0].adverse_flow_direction_correlation = 2.0;

    CHECK_THROWS_AS(sim::TickLoop(world, config), std::invalid_argument);
}

TEST_CASE("The constructor rejects a config that does not match the world",
          "[simulation][tick_loop]") {
    sim::World world(make_config({.assets = 1}));

    CHECK_THROWS_AS(sim::TickLoop(world, make_config({.assets = 2})), std::invalid_argument);
}

TEST_CASE("Building a loop does not consume the real adverse-flow stream",
          "[simulation][tick_loop]") {
    // The constructor validates the adverse-flow spec with a dry run on a COPY
    // of the engine: the real stream must still start from its first word.
    const sim::SimulationConfig config = make_config({.lambda = 3.0});
    sim::World with_loop(config);
    sim::World untouched(config);
    const sim::TickLoop loop(with_loop, config);

    CHECK(with_loop.adverse_flow_engine(0)() == untouched.adverse_flow_engine(0)());
}

// ------------------------------------------------------------------- basics

TEST_CASE("Each step advances the tick count and the time by exactly dt",
          "[simulation][tick_loop]") {
    sim::SimulationConfig config = make_config();
    config.dt = 0.1;
    sim::World world(config);
    sim::TickLoop loop(world, config);

    for (int i = 0; i < 7; ++i) {
        loop.step();
    }

    CHECK(loop.tick_count() == 7);
    CHECK(loop.time() == 7.0 * 0.1); // k * dt, not a sum of seven rounded additions
}

TEST_CASE("A tick notifies in a fixed order, and a first tick publishes the new quotes",
          "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    sim::TickLoop loop(world, config);
    Recorder recorder;
    loop.add_observer(recorder);

    loop.step();

    const std::vector<std::string> expected{"start", "mark", "quote", "end"};
    CHECK(recorder.sequence == expected);
    CHECK(recorder.starts == std::vector<double>{1.0});
    CHECK(recorder.ends == std::vector<double>{1.0});
    REQUIRE(recorder.marks.size() == 1);
    CHECK(recorder.marks[0].first == 0);
    CHECK(recorder.marks[0].second == Catch::Approx(100.0).margin(1e-6));
}

TEST_CASE("With no latency the first tick puts both quotes live, in the book",
          "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    sim::TickLoop loop(world, config);

    loop.step();

    const mm::AssetLiveQuotes& live = loop.live_quotes()[0];
    REQUIRE(live.bid.has_value());
    REQUIRE(live.ask.has_value());
    CHECK(live.bid->quote.quantity == 10);
    CHECK(live.ask->quote.quantity == 10);
    CHECK(live.bid->quote.price < mid_of(world));
    CHECK(live.ask->quote.price > mid_of(world));
    check_consistent(world, loop);
}

TEST_CASE("A quote that is still good is left alone, at threshold 1", "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    sim::TickLoop loop(world, config);
    loop.step();
    const mm::OrderId bid_id = loop.live_quotes()[0].bid->id;
    const mm::OrderId ask_id = loop.live_quotes()[0].ask->id;
    Recorder recorder;
    loop.add_observer(recorder);

    loop.step();

    CHECK(loop.live_quotes()[0].bid->id == bid_id);
    CHECK(loop.live_quotes()[0].ask->id == ask_id);
    const std::vector<std::string> expected{
        "start", "mark", "end"}; // no quote event: nothing changed
    CHECK(recorder.sequence == expected);
}

TEST_CASE("At threshold 0 every quote is replaced at every tick", "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config({.threshold = 0});
    sim::World world(config);
    sim::TickLoop loop(world, config);
    loop.step();
    const mm::OrderId bid_id = loop.live_quotes()[0].bid->id;
    const mm::OrderId ask_id = loop.live_quotes()[0].ask->id;

    loop.step();

    CHECK(loop.live_quotes()[0].bid->id != bid_id);
    CHECK(loop.live_quotes()[0].ask->id != ask_id);
    check_consistent(world, loop);
}

// ------------------------------------------------------------ adverse fills

TEST_CASE("An adverse buy takes part of the ask: position, spread capture and notifications",
          "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    sim::TickLoop loop(world, config);
    loop.step();
    const mm::Price ask_price = loop.live_quotes()[0].ask->quote.price;
    Recorder recorder;
    loop.add_observer(recorder);

    inject_adverse(world, 0, mm::Side::Buy, 4);
    loop.step();

    // The market maker sold 4 at its ask, 6 of the quote still rests.
    CHECK(world.pnl_ledger().position(0).quantity() == -4);
    REQUIRE(loop.live_quotes()[0].ask.has_value());
    CHECK(loop.live_quotes()[0].ask->quote.quantity == 6);
    CHECK(loop.live_quotes()[0].bid->quote.quantity == 10); // the bid is untouched
    check_consistent(world, loop);

    // Sold above the mid: positive spread capture of (ask - mid) per share.
    CHECK(world.pnl_ledger().breakdown(0).spread_capture ==
          Catch::Approx((ask_price - mid_of(world)) * 4.0).margin(1e-6));

    REQUIRE(recorder.trades.size() == 1);
    CHECK(recorder.trades[0].asset == 0);
    CHECK(recorder.trades[0].trade.quantity == 4);
    CHECK(recorder.trades[0].trade.price == ask_price); // the maker's price
    CHECK_FALSE(recorder.trades[0].aggressor_is_mm);    // the aggressor was adverse flow

    // The trade comes before the quote change it caused.
    const std::vector<std::string> expected{"start", "mark", "trade", "quote", "end"};
    CHECK(recorder.sequence == expected);
    REQUIRE(recorder.quotes.size() == 1);
    CHECK(recorder.quotes[0].second.ask->quantity == 6);
}

TEST_CASE("An adverse sell takes part of the bid", "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    sim::TickLoop loop(world, config);
    loop.step();
    const mm::Price bid_price = loop.live_quotes()[0].bid->quote.price;

    inject_adverse(world, 0, mm::Side::Sell, 3);
    loop.step();

    CHECK(world.pnl_ledger().position(0).quantity() == 3); // long 3
    CHECK(loop.live_quotes()[0].bid->quote.quantity == 7);
    // Bought below the mid: positive spread capture of (mid - bid) per share.
    CHECK(world.pnl_ledger().breakdown(0).spread_capture ==
          Catch::Approx((mid_of(world) - bid_price) * 3.0).margin(1e-6));
    check_consistent(world, loop);
}

TEST_CASE("An adverse order larger than the quote empties it and the rest is discarded",
          "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    sim::TickLoop loop(world, config);
    loop.step();
    Recorder recorder;
    loop.add_observer(recorder);

    inject_adverse(world, 0, mm::Side::Buy, 25); // the ask only holds 10
    loop.step();

    CHECK(world.pnl_ledger().position(0).quantity() == -10); // IOC: the 15 left over do not rest
    CHECK_FALSE(loop.live_quotes()[0].ask.has_value());
    CHECK(world.matching_engine(0).book().empty(mm::Side::Sell));
    check_consistent(world, loop);
    REQUIRE(recorder.quotes.size() == 1);
    CHECK_FALSE(recorder.quotes[0].second.ask.has_value()); // published as "no ask"

    loop.step(); // and the empty side is quoted again
    CHECK(loop.live_quotes()[0].ask.has_value());
    check_consistent(world, loop);
}

TEST_CASE("An adverse order with nothing to trade against does nothing",
          "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    sim::TickLoop loop(world, config);
    Recorder recorder;
    loop.add_observer(recorder);

    // Nothing is live yet at the start of the first tick.
    inject_adverse(world, 0, mm::Side::Buy, 5);
    loop.step();

    CHECK(world.pnl_ledger().position(0).quantity() == 0);
    CHECK(recorder.trades.empty());
}

TEST_CASE("A maker fee is charged on what the market maker traded", "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config({.fee_bps = 10.0});
    sim::World world(config);
    sim::TickLoop loop(world, config);
    loop.step();
    const mm::Price ask_price = loop.live_quotes()[0].ask->quote.price;

    inject_adverse(world, 0, mm::Side::Buy, 4);
    loop.step();

    CHECK(world.pnl_ledger().breakdown(0).fees ==
          Catch::Approx(-ask_price * 4.0 * 10.0 * 1.0e-4).margin(1e-9));
}

TEST_CASE("After a fill the quote is restocked and re-centred on the inventory",
          "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    sim::TickLoop loop(world, config);
    loop.step();
    inject_adverse(world, 0, mm::Side::Buy, 4);
    loop.step();
    const mm::OrderId old_ask = loop.live_quotes()[0].ask->id;
    const mm::Price old_bid_price = loop.live_quotes()[0].bid->quote.price;

    loop.step(); // sees quantity 6 against 10 wanted, and a short position

    CHECK(loop.live_quotes()[0].ask->id != old_ask);
    CHECK(loop.live_quotes()[0].ask->quote.quantity == 10);
    // Short 4: the reservation price rises, so the bid moves up with it.
    CHECK(loop.live_quotes()[0].bid->quote.price > old_bid_price);
    check_consistent(world, loop);
}

// ------------------------------------------- stale quotes, the point of it all

TEST_CASE("Adverse flow hits the PREVIOUS tick's quote, not the freshly updated one",
          "[simulation][tick_loop]") {
    // The price jumps by 5 every tick. The market maker re-quotes around the
    // new price, but adverse flow is executed before that, against the old
    // quote: the adverse selection the whole simulation exists to measure.
    const sim::SimulationConfig config = make_config({.mu = 5.0});
    sim::World world(config);
    sim::TickLoop loop(world, config);
    loop.step();
    const mm::Price stale_ask = loop.live_quotes()[0].ask->quote.price;
    Recorder recorder;
    loop.add_observer(recorder);

    inject_adverse(world, 0, mm::Side::Buy, 3);
    loop.step();

    REQUIRE(recorder.trades.size() == 1);
    CHECK(recorder.trades[0].trade.price == stale_ask);
    CHECK(recorder.trades[0].trade.price < mid_of(world) - 4.0); // far below the new mid
    // Sold below the new mid: the spread capture of this fill is NEGATIVE.
    CHECK(world.pnl_ledger().breakdown(0).spread_capture < 0.0);
}

TEST_CASE("Generated adverse flow after an up move with full correlation only ever buys",
          "[simulation][tick_loop]") {
    // Rising price, correlation 1: every adverse order is a buy, so the market
    // maker only ever sells and its position can only be zero or negative.
    const sim::SimulationConfig config =
        make_config({.mu = 5.0, .lambda = 6.0, .correlation = 1.0});
    sim::World world(config);
    sim::TickLoop loop(world, config);
    Recorder recorder;
    loop.add_observer(recorder);

    std::size_t checked = 0;
    for (int i = 0; i < 30; ++i) {
        const std::optional<mm::LiveQuote> ask_before = loop.live_quotes()[0].ask;
        loop.step();
        CHECK(world.pnl_ledger().position(0).quantity() <= 0);
        check_consistent(world, loop);

        // Generated adverse flow is scheduled before the quote requests too, so
        // it trades at the ask that was live BEFORE this tick, never at the one
        // placed during it (the price moves by 5 per tick, they cannot coincide).
        for (; checked < recorder.trades.size(); ++checked) {
            REQUIRE(ask_before.has_value());
            CHECK(recorder.trades[checked].trade.price == ask_before->quote.price);
        }
    }

    CHECK(world.pnl_ledger().position(0).quantity() < 0);
    REQUIRE_FALSE(recorder.trades.empty());
    for (const Recorder::TradeSeen& seen : recorder.trades) {
        CHECK_FALSE(seen.aggressor_is_mm);
    }
}

TEST_CASE("Full-correlation adverse flow follows each tick's OWN price move, up then down",
          "[simulation][tick_loop]") {
    // A real volatility, so the price goes up on some ticks and down on others.
    // With correlation 1 and no latency, all adverse orders of a tick go the
    // same way: after an up move they buy (the market maker sells), after a
    // down move they sell (the market maker buys). A move measured from the
    // first price, or from a stale one, would often have the wrong sign.
    const sim::SimulationConfig config =
        make_config({.lambda = 6.0, .correlation = 1.0, .volatility = 1.0, .seed = 3});
    sim::World world(config);
    sim::TickLoop loop(world, config);
    mm::Quantity position_before = 0;
    mm::Price mid_before = mid_of(world);
    int up_ticks = 0;
    int down_ticks = 0;
    int ticks_with_fills = 0;

    for (int tick = 0; tick < 60; ++tick) {
        loop.step();
        const double move = mid_of(world) - mid_before;
        const mm::Quantity delta = world.pnl_ledger().position(0).quantity() - position_before;

        CHECK(static_cast<double>(delta) * move <= 0.0); // up: delta < 0, down: delta > 0
        ticks_with_fills += (delta != 0) ? 1 : 0;
        (move > 0.0 ? up_ticks : down_ticks) += 1;
        position_before = world.pnl_ledger().position(0).quantity();
        mid_before = mid_of(world);
    }

    CHECK(up_ticks > 10);
    CHECK(down_ticks > 10);
    CHECK(ticks_with_fills > 15); // the property was actually exercised
}

// ----------------------------------------------------------------- latency

TEST_CASE("With latency a request is in flight, not live, and is not sent twice",
          "[simulation][tick_loop]") {
    const sim::SimulationConfig config =
        make_config({.quote_delay = sim::ConstantDelaySpec{.delay = 1.0}});
    sim::World world(config);
    sim::TickLoop loop(world, config);

    loop.step(); // two submits scheduled for time 2: nothing live yet

    CHECK_FALSE(loop.live_quotes()[0].bid.has_value());
    CHECK_FALSE(loop.live_quotes()[0].ask.has_value());
    CHECK(world.event_queue().size() == 2);

    loop.step(); // the two submits execute; the busy sides are NOT planned again

    CHECK(loop.live_quotes()[0].bid.has_value());
    CHECK(loop.live_quotes()[0].ask.has_value());
    CHECK(world.event_queue().empty());
    CHECK(world.matching_engine(0).book().size(mm::Side::Buy) == 1);
    CHECK(world.matching_engine(0).book().size(mm::Side::Sell) == 1);
    check_consistent(world, loop);
}

TEST_CASE("A cancel that arrives after its quote was filled away is harmless",
          "[simulation][tick_loop]") {
    // Price rising by 5, latency 1 tick: at tick 3 both quotes are replaced
    // (a cancel and a submit each, due at 4), but an adverse buy empties the
    // ask first. The cancel then finds nothing, and the submit lands on an
    // empty side.
    const sim::SimulationConfig config =
        make_config({.mu = 5.0, .quote_delay = sim::ConstantDelaySpec{.delay = 1.0}});
    sim::World world(config);
    sim::TickLoop loop(world, config);
    loop.step();
    loop.step(); // the first quotes are live
    REQUIRE(loop.live_quotes()[0].ask.has_value());

    inject_adverse(world, 0, mm::Side::Buy, 50);
    loop.step(); // the ask is taken (10), its cancel is still in flight

    CHECK(world.pnl_ledger().position(0).quantity() == -10);
    CHECK_FALSE(loop.live_quotes()[0].ask.has_value());

    CHECK_NOTHROW(loop.step()); // the cancel finds nothing, the submit finds an empty side
    CHECK(loop.live_quotes()[0].ask.has_value());
    CHECK(world.event_queue().empty());
    check_consistent(world, loop);
    CHECK_NOTHROW(loop.step());               // and the side is no longer stuck as busy:
    CHECK_FALSE(world.event_queue().empty()); // the next replacement was planned and sent
}

TEST_CASE("With random latency, adverse flow and drift, the loop's view always matches the books",
          "[simulation][tick_loop]") {
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        const sim::SimulationConfig config =
            make_config({.mu = 3.0,
                         .lambda = 3.0,
                         .correlation = 0.3,
                         .quote_delay = sim::UniformDelaySpec{.min = 0.0, .max = 2.0},
                         .adverse_delay = sim::ExponentialDelaySpec{.mean = 0.5},
                         .seed = seed});
        sim::World world(config);
        sim::TickLoop loop(world, config);

        for (int tick = 0; tick < 60; ++tick) {
            loop.step();
            check_consistent(world, loop); // never two orders on a side, never an untracked one
        }
    }
}

// --------------------------------------------------------------- self-trade

TEST_CASE("Crossing its own stale quote is booked on both sides and nets to nothing",
          "[simulation][tick_loop]") {
    // The price jumps by 10 per tick and the two quote sides are delayed
    // independently, so a new bid can reach the book while the old ask still
    // rests below it: the market maker trades with itself. With NO adverse
    // flow every trade in this run is such a self-trade, so the position and
    // the gross P&L must stay exactly zero, whatever the interleaving.
    bool saw_self_trade = false;

    for (std::uint64_t seed = 1; seed <= 120; ++seed) {
        const sim::SimulationConfig config =
            make_config({.mu = 10.0,
                         .quote_delay = sim::UniformDelaySpec{.min = 0.0, .max = 2.0},
                         .seed = seed});
        sim::World world(config);
        sim::TickLoop loop(world, config);
        Recorder recorder;
        loop.add_observer(recorder);

        for (int tick = 0; tick < 8; ++tick) {
            loop.step();
            CHECK(world.pnl_ledger().position(0).quantity() == 0);
            CHECK(world.pnl_ledger().breakdown(0).gross() == Catch::Approx(0.0).margin(1e-9));
            check_consistent(world, loop);
        }
        for (const Recorder::TradeSeen& seen : recorder.trades) {
            CHECK(seen.aggressor_is_mm); // nothing else can trade here
            saw_self_trade = true;
        }
    }

    CHECK(saw_self_trade); // the scenario really was exercised
}

// ------------------------------------------------------------ determinism etc.

namespace {

struct RunSummary {
    mm::Quantity position;
    std::size_t trades;
    std::uint64_t last_bid_id;
    friend bool operator==(const RunSummary&, const RunSummary&) = default;
};

RunSummary run(std::uint64_t seed) {
    const sim::SimulationConfig config =
        make_config({.mu = 2.0,
                     .lambda = 4.0,
                     .correlation = 0.5,
                     .quote_delay = sim::UniformDelaySpec{.min = 0.0, .max = 1.5},
                     .adverse_delay = sim::ExponentialDelaySpec{.mean = 0.4},
                     .seed = seed});
    sim::World world(config);
    sim::TickLoop loop(world, config);
    Recorder recorder;
    loop.add_observer(recorder);
    for (int tick = 0; tick < 50; ++tick) {
        loop.step();
    }
    const std::optional<mm::LiveQuote>& bid = loop.live_quotes()[0].bid;
    return RunSummary{.position = world.pnl_ledger().position(0).quantity(),
                      .trades = recorder.trades.size(),
                      .last_bid_id = bid.has_value() ? static_cast<std::uint64_t>(bid->id) : 0U};
}

} // namespace

TEST_CASE("The same config and seed reproduce the same run exactly", "[simulation][tick_loop]") {
    CHECK(run(5) == run(5));
    CHECK(run(9) == run(9));
}

TEST_CASE("A different seed gives a different run", "[simulation][tick_loop]") {
    CHECK_FALSE(run(5) == run(6));
}

TEST_CASE("Each asset is booked on its own", "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config({.assets = 2});
    sim::World world(config);
    sim::TickLoop loop(world, config);
    loop.step();
    REQUIRE(loop.live_quotes()[1].ask.has_value());
    Recorder recorder;
    loop.add_observer(recorder);

    inject_adverse(world, 1, mm::Side::Buy, 4);
    loop.step();

    CHECK(world.pnl_ledger().position(0).quantity() == 0);
    CHECK(world.pnl_ledger().position(1).quantity() == -4);
    CHECK(loop.live_quotes()[0].ask->quote.quantity == 10);
    CHECK(loop.live_quotes()[1].ask->quote.quantity == 6);
    REQUIRE(recorder.trades.size() == 1);
    CHECK(recorder.trades[0].asset == 1);
    check_consistent(world, loop);
}

TEST_CASE("Cancels of the same OrderId on different assets in one tick do not collide",
          "[simulation][tick_loop]") {
    // Every engine numbers its orders from 1, so two assets hold orders with
    // the same id. The price rises on both, so both assets cancel and replace
    // their quotes in the same tick: two cancels carrying the same id value.
    const sim::SimulationConfig config = make_config({.assets = 2, .mu = 5.0});
    sim::World world(config);
    sim::TickLoop loop(world, config);

    for (int tick = 0; tick < 6; ++tick) {
        CHECK_NOTHROW(loop.step());
        check_consistent(world, loop);
    }
    // Both assets really did replace their quotes (the ids are those of replacements).
    CHECK(loop.live_quotes()[0].bid->id == loop.live_quotes()[1].bid->id);
}

TEST_CASE("Every observer sees the same run", "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config({.lambda = 3.0});
    sim::World world(config);
    sim::TickLoop loop(world, config);
    Recorder first;
    Recorder second;
    loop.add_observer(first);
    loop.add_observer(second);

    for (int tick = 0; tick < 10; ++tick) {
        loop.step();
    }

    CHECK(first.sequence == second.sequence);
    CHECK(first.sequence.size() > 20);
}
