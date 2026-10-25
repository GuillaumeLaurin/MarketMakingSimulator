/// \file
/// Tests for simulation/tick_loop.hpp. The scenarios are deliberately
/// controllable: the true price barely moves (volatility 1e-9) except for a
/// chosen drift, adverse flow is mostly a SCRIPTED taker that sends one order before a
/// step (so a fill's size and instant are exact), and the market
/// maker's quotes are read back from loop.quotes_of(maker_id) and from the books.
/// The properties that matter most are checked as invariants over many ticks
/// and seeds: what the loop believes it has live is exactly what rests in the
/// books, even with random latency, races between a cancel and a fill, and
/// the market maker crossing its own stale quote.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
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

#include <math/process/dynamics.hpp>
#include <math/rng/seed.hpp>
#include <math/rng/stream.hpp>
#include <math/types.hpp>
#include <simulation/asset_spec.hpp>
#include <simulation/delay_spec.hpp>
#include <simulation/simulation_config.hpp>
#include <simulation/simulation_observer.hpp>
#include <simulation/tick_loop.hpp>
#include <simulation/world.hpp>

#include "maker.hpp"
#include "scripted.hpp"

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
                          .factor_model = std::nullopt};
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
        .process = sim::RandomWalkSpec{.dynamics = mms::math::process::Dynamics::Arithmetic},
        .agents = {sim::tests::informed_agent(knobs.lambda, knobs.correlation, knobs.adverse_delay),
                   sim::tests::maker_agent(
                       strategy_for(knobs.assets), knobs.threshold, knobs.quote_delay)},
        .master_seed = math::rng::Seed{knobs.seed},
        .dt = 1.0,
        .tick_count = 100};
}

/// Records everything a run notifies, in order.
struct Recorder final : sim::SimulationObserver {
    struct TradeSeen {
        mm::AssetId asset;
        mm::Trade trade;
        sim::TradeParties parties;
    };

    std::vector<std::string> sequence;
    std::vector<double> starts;
    std::vector<double> ends;
    std::vector<std::pair<mm::AssetId, mm::Price>> marks;
    std::vector<TradeSeen> trades;

    void on_tick_start(double time) override {
        sequence.emplace_back("start");
        starts.push_back(time);
    }

    void on_mark(mm::AssetId asset, mm::Price mid) override {
        sequence.emplace_back("mark");
        marks.emplace_back(asset, mid);
    }

    void
    on_trade(mm::AssetId asset, const mm::Trade& trade, const sim::TradeParties& parties) override {
        sequence.emplace_back("trade");
        trades.push_back({.asset = asset, .trade = trade, .parties = parties});
    }

    void on_tick_end(double time) override {
        sequence.emplace_back("end");
        ends.push_back(time);
    }
};

/// The place of the market maker among the agents of make_config; the informed trader that
/// comes first reaches the exchange first at equal times.
constexpr sim::AgentId maker_id = 1;

/// Takes the place of the informed trader of make_config (agent 0) with a scripted one that
/// sends one Market IOC order at the next step, before anything else the loop schedules.
struct Taker {
    explicit Taker(sim::World& world) {
        auto agent = std::make_unique<sim::tests::Scripted>();
        script = agent.get();
        script->once = true;
        const math::Vector& prices = world.price_process().prices();
        std::vector<mm::LedgerAsset> ledger_assets;
        for (Eigen::Index i = 0; i < prices.size(); ++i) {
            ledger_assets.push_back({.initial_mid = prices(i)});
        }
        const math::rng::RngHub hub(math::rng::Seed{99});
        const math::rng::AssetKey key = math::rng::asset_key_of("taker");
        world.agent(0) = sim::AgentRuntime(
            0,
            world.agent(0).spec(),
            sim::AccountRules{},
            ledger_assets,
            hub.engine_for({.subsystem = math::rng::Subsystem::Agent, .asset = key}),
            hub.engine_for({.subsystem = math::rng::Subsystem::AgentLatency, .asset = key}),
            std::move(agent));
    }

    void market(mm::AssetId asset, mm::Side side, mm::Quantity quantity) {
        script->to_ask = {mms::agents::SubmitMarket{.asset = asset,
                                                    .side = side,
                                                    .quantity = quantity,
                                                    .time_in_force = mm::MarketTimeInForce::IOC}};
    }

    sim::tests::Scripted* script = nullptr;
};

/// The loop's belief about what is live must be exactly what rests in the books.
void check_consistent(const sim::World& world, const sim::TickLoop& loop) {
    std::size_t resting = 0;
    for (mm::AssetId asset = 0; asset < world.asset_count(); ++asset) {
        const mm::OrderBook& book = world.matching_engine(asset).book();
        const mm::LiveQuoteSet live_set = loop.quotes_of(maker_id);
        const mm::AssetLiveQuotes& live = live_set[asset];
        for (const mm::Side side : {mm::Side::Buy, mm::Side::Sell}) {
            const std::optional<mm::LiveQuote>& quote = side == mm::Side::Buy ? live.bid : live.ask;
            if (quote.has_value()) {
                CHECK(book.size(side) == 1);
                CHECK(book.depth_at(side, quote->quote.price) == quote->quote.quantity);
            } else {
                CHECK(book.size(side) == 0);
            }
            resting += book.size(side);
        }
    }
    // And every order that rests has its owner recorded, no more and no fewer.
    CHECK(loop.recorded_orders() == resting);
}

/// Counts the cancels of orders the market maker never asked to cancel: the ones the
/// engine makes itself to stop the market maker trading with itself.
struct SelfCancelWatcher final : mm::MatchingEngineObserver {
    explicit SelfCancelWatcher(const sim::World& world) : world_(world) {}

    void on_order_cancelled(mm::OrderId id) override {
        for (const mms::agents::OpenOrder& order : world_.agent(maker_id).orders().orders()) {
            if (order.id == id && order.state == mms::agents::OrderState::Resting) {
                ++self_cancels;
            }
        }
    }

    int self_cancels = 0;

private:
    const sim::World& world_;
};

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
    REQUIRE(loop.quotes_of(maker_id).size() == 2);
    for (const mm::AssetLiveQuotes& live : loop.quotes_of(maker_id)) {
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

TEST_CASE("The constructor rejects a config that does not match the world",
          "[simulation][tick_loop]") {
    sim::World world(make_config({.assets = 1}));

    CHECK_THROWS_AS(sim::TickLoop(world, make_config({.assets = 2})), std::invalid_argument);
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

TEST_CASE("A tick notifies in a fixed order", "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    sim::TickLoop loop(world, config);
    Recorder recorder;
    loop.add_observer(recorder);

    loop.step();

    const std::vector<std::string> expected{"start", "mark", "end"};
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

    const mm::AssetLiveQuotes live = loop.quotes_of(maker_id)[0];
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
    const mm::OrderId bid_id = loop.quotes_of(maker_id)[0].bid->id;
    const mm::OrderId ask_id = loop.quotes_of(maker_id)[0].ask->id;
    Recorder recorder;
    loop.add_observer(recorder);

    loop.step();

    CHECK(loop.quotes_of(maker_id)[0].bid->id == bid_id);
    CHECK(loop.quotes_of(maker_id)[0].ask->id == ask_id);
    const std::vector<std::string> expected{
        "start", "mark", "end"}; // no quote event: nothing changed
    CHECK(recorder.sequence == expected);
}

TEST_CASE("At threshold 0 every quote is replaced at every tick", "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config({.threshold = 0});
    sim::World world(config);
    sim::TickLoop loop(world, config);
    loop.step();
    const mm::OrderId bid_id = loop.quotes_of(maker_id)[0].bid->id;
    const mm::OrderId ask_id = loop.quotes_of(maker_id)[0].ask->id;

    loop.step();

    CHECK(loop.quotes_of(maker_id)[0].bid->id != bid_id);
    CHECK(loop.quotes_of(maker_id)[0].ask->id != ask_id);
    check_consistent(world, loop);
}

// ------------------------------------------------------------ adverse fills

TEST_CASE("An adverse buy takes part of the ask: position, spread capture and notifications",
          "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    Taker taker(world);
    sim::TickLoop loop(world, config);
    loop.step();
    const mm::Price ask_price = loop.quotes_of(maker_id)[0].ask->quote.price;
    Recorder recorder;
    loop.add_observer(recorder);

    taker.market(0, mm::Side::Buy, 4);
    loop.step();

    // The market maker sold 4 at its ask, 6 of the quote still rests.
    CHECK(world.agent(maker_id).account().ledger().position(0).quantity() == -4);
    REQUIRE(loop.quotes_of(maker_id)[0].ask.has_value());
    CHECK(loop.quotes_of(maker_id)[0].ask->quote.quantity == 6);
    CHECK(loop.quotes_of(maker_id)[0].bid->quote.quantity == 10); // the bid is untouched
    check_consistent(world, loop);

    // Sold above the mid: positive spread capture of (ask - mid) per share.
    CHECK(world.agent(maker_id).account().ledger().breakdown(0).spread_capture ==
          Catch::Approx((ask_price - mid_of(world)) * 4.0).margin(1e-6));

    REQUIRE(recorder.trades.size() == 1);
    CHECK(recorder.trades[0].asset == 0);
    CHECK(recorder.trades[0].trade.quantity == 4);
    CHECK(recorder.trades[0].trade.price == ask_price);  // the maker's price
    CHECK(recorder.trades[0].parties.aggressive() == 0); // the taker started it
    CHECK(recorder.trades[0].parties.passive() == maker_id);
    CHECK(recorder.trades[0].parties.buyer == 0);
    CHECK(recorder.trades[0].parties.seller == maker_id);
    CHECK(recorder.trades[0].parties.aggressor == mm::Side::Buy);

    const std::vector<std::string> expected{"start", "mark", "trade", "end"};
    CHECK(recorder.sequence == expected);
    CHECK(loop.quotes_of(maker_id)[0].ask->quote.quantity == 6);
}

TEST_CASE("An adverse sell takes part of the bid", "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    Taker taker(world);
    sim::TickLoop loop(world, config);
    loop.step();
    const mm::Price bid_price = loop.quotes_of(maker_id)[0].bid->quote.price;

    taker.market(0, mm::Side::Sell, 3);
    loop.step();

    CHECK(world.agent(maker_id).account().ledger().position(0).quantity() == 3); // long 3
    CHECK(loop.quotes_of(maker_id)[0].bid->quote.quantity == 7);
    // Bought below the mid: positive spread capture of (mid - bid) per share.
    CHECK(world.agent(maker_id).account().ledger().breakdown(0).spread_capture ==
          Catch::Approx((mid_of(world) - bid_price) * 3.0).margin(1e-6));
    check_consistent(world, loop);
}

TEST_CASE("An adverse order larger than the quote empties it and the rest is discarded",
          "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    Taker taker(world);
    sim::TickLoop loop(world, config);
    loop.step();
    Recorder recorder;
    loop.add_observer(recorder);

    taker.market(0, mm::Side::Buy, 25); // the ask only holds 10
    loop.step();

    CHECK(world.agent(maker_id).account().ledger().position(0).quantity() ==
          -10); // IOC: the 15 left over do not rest
    CHECK_FALSE(loop.quotes_of(maker_id)[0].ask.has_value());
    CHECK(world.matching_engine(0).book().empty(mm::Side::Sell));
    check_consistent(world, loop);

    loop.step(); // and the empty side is quoted again
    CHECK(loop.quotes_of(maker_id)[0].ask.has_value());
    check_consistent(world, loop);
}

TEST_CASE("An adverse order with nothing to trade against does nothing",
          "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    Taker taker(world);
    sim::TickLoop loop(world, config);
    Recorder recorder;
    loop.add_observer(recorder);

    // Nothing is live yet at the start of the first tick.
    taker.market(0, mm::Side::Buy, 5);
    loop.step();

    CHECK(world.agent(maker_id).account().ledger().position(0).quantity() == 0);
    CHECK(recorder.trades.empty());
}

TEST_CASE("A maker fee is charged on what the market maker traded", "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config({.fee_bps = 10.0});
    sim::World world(config);
    Taker taker(world);
    sim::TickLoop loop(world, config);
    loop.step();
    const mm::Price ask_price = loop.quotes_of(maker_id)[0].ask->quote.price;

    taker.market(0, mm::Side::Buy, 4);
    loop.step();

    CHECK(world.agent(maker_id).account().ledger().breakdown(0).fees ==
          Catch::Approx(-ask_price * 4.0 * 10.0 * 1.0e-4).margin(1e-9));
}

TEST_CASE("After a fill the quote is restocked and re-centred on the inventory",
          "[simulation][tick_loop]") {
    const sim::SimulationConfig config = make_config();
    sim::World world(config);
    Taker taker(world);
    sim::TickLoop loop(world, config);
    loop.step();
    taker.market(0, mm::Side::Buy, 4);
    loop.step();
    const mm::OrderId old_ask = loop.quotes_of(maker_id)[0].ask->id;
    const mm::Price old_bid_price = loop.quotes_of(maker_id)[0].bid->quote.price;

    loop.step(); // sees quantity 6 against 10 wanted, and a short position

    CHECK(loop.quotes_of(maker_id)[0].ask->id != old_ask);
    CHECK(loop.quotes_of(maker_id)[0].ask->quote.quantity == 10);
    // Short 4: the reservation price rises, so the bid moves up with it.
    CHECK(loop.quotes_of(maker_id)[0].bid->quote.price > old_bid_price);
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
    Taker taker(world);
    sim::TickLoop loop(world, config);
    loop.step();
    const mm::Price stale_ask = loop.quotes_of(maker_id)[0].ask->quote.price;
    Recorder recorder;
    loop.add_observer(recorder);

    taker.market(0, mm::Side::Buy, 3);
    loop.step();

    REQUIRE(recorder.trades.size() == 1);
    CHECK(recorder.trades[0].trade.price == stale_ask);
    CHECK(recorder.trades[0].trade.price < mid_of(world) - 4.0); // far below the new mid
    // Sold below the new mid: the spread capture of this fill is NEGATIVE.
    CHECK(world.agent(maker_id).account().ledger().breakdown(0).spread_capture < 0.0);
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
        const std::optional<mm::LiveQuote> ask_before = loop.quotes_of(maker_id)[0].ask;
        loop.step();
        CHECK(world.agent(maker_id).account().ledger().position(0).quantity() <= 0);
        check_consistent(world, loop);

        // Generated adverse flow is scheduled before the quote requests too, so
        // it trades at the ask that was live BEFORE this tick, never at the one
        // placed during it (the price moves by 5 per tick, they cannot coincide).
        for (; checked < recorder.trades.size(); ++checked) {
            REQUIRE(ask_before.has_value());
            CHECK(recorder.trades[checked].trade.price == ask_before->quote.price);
        }
    }

    CHECK(world.agent(maker_id).account().ledger().position(0).quantity() < 0);
    REQUIRE_FALSE(recorder.trades.empty());
    for (const Recorder::TradeSeen& seen : recorder.trades) {
        CHECK(seen.parties.aggressive() != maker_id);
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
        const mm::Quantity delta =
            world.agent(maker_id).account().ledger().position(0).quantity() - position_before;

        CHECK(static_cast<double>(delta) * move <= 0.0); // up: delta < 0, down: delta > 0
        ticks_with_fills += (delta != 0) ? 1 : 0;
        (move > 0.0 ? up_ticks : down_ticks) += 1;
        position_before = world.agent(maker_id).account().ledger().position(0).quantity();
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

    CHECK_FALSE(loop.quotes_of(maker_id)[0].bid.has_value());
    CHECK_FALSE(loop.quotes_of(maker_id)[0].ask.has_value());
    CHECK(world.event_queue().size() == 2);

    loop.step(); // the two submits execute; the busy sides are NOT planned again

    CHECK(loop.quotes_of(maker_id)[0].bid.has_value());
    CHECK(loop.quotes_of(maker_id)[0].ask.has_value());
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
    Taker taker(world);
    sim::TickLoop loop(world, config);
    loop.step();
    loop.step(); // the first quotes are live
    REQUIRE(loop.quotes_of(maker_id)[0].ask.has_value());

    taker.market(0, mm::Side::Buy, 50);
    loop.step(); // the ask is taken (10), its cancel is still in flight

    CHECK(world.agent(maker_id).account().ledger().position(0).quantity() == -10);
    CHECK_FALSE(loop.quotes_of(maker_id)[0].ask.has_value());

    CHECK_NOTHROW(loop.step()); // the cancel finds nothing, the submit finds an empty side
    CHECK(loop.quotes_of(maker_id)[0].ask.has_value());
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

TEST_CASE("Crossing its own stale quote cancels that quote instead of trading with itself",
          "[simulation][tick_loop]") {
    // The price jumps by 10 per tick and the two quote sides are delayed
    // independently, so a new bid can reach the book while the old ask still
    // rests below it. Self-trade prevention cancels the older order, the stale ask,
    // and the new bid rests. With NO adverse flow nothing else can trade, so there is
    // no trade at all, and the position and the gross P&L stay exactly zero.
    int self_cancels = 0;

    for (std::uint64_t seed = 1; seed <= 120; ++seed) {
        const sim::SimulationConfig config =
            make_config({.mu = 10.0,
                         .quote_delay = sim::UniformDelaySpec{.min = 0.0, .max = 2.0},
                         .seed = seed});
        sim::World world(config);
        // Added before the loop's own, so it sees a cancel while the order is still listed.
        SelfCancelWatcher watcher(world);
        world.matching_engine(0).add_observer(watcher);
        sim::TickLoop loop(world, config);
        Recorder recorder;
        loop.add_observer(recorder);

        for (int tick = 0; tick < 8; ++tick) {
            loop.step();
            CHECK(world.agent(maker_id).account().ledger().position(0).quantity() == 0);
            CHECK(world.agent(maker_id).account().ledger().breakdown(0).gross() ==
                  Catch::Approx(0.0).margin(1e-9));
            check_consistent(world, loop);
        }
        CHECK(recorder.trades.empty());
        self_cancels += watcher.self_cancels;
    }

    CHECK(self_cancels > 0); // the scenario really was exercised
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
    const std::optional<mm::LiveQuote> bid = loop.quotes_of(maker_id)[0].bid;
    return RunSummary{.position = world.agent(maker_id).account().ledger().position(0).quantity(),
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
    Taker taker(world);
    sim::TickLoop loop(world, config);
    loop.step();
    REQUIRE(loop.quotes_of(maker_id)[1].ask.has_value());
    Recorder recorder;
    loop.add_observer(recorder);

    taker.market(1, mm::Side::Buy, 4);
    loop.step();

    CHECK(world.agent(maker_id).account().ledger().position(0).quantity() == 0);
    CHECK(world.agent(maker_id).account().ledger().position(1).quantity() == -4);
    CHECK(loop.quotes_of(maker_id)[0].ask->quote.quantity == 10);
    CHECK(loop.quotes_of(maker_id)[1].ask->quote.quantity == 6);
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
    CHECK(loop.quotes_of(maker_id)[0].bid->id == loop.quotes_of(maker_id)[1].bid->id);
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

// ------------------------------------------------------------------- agents

namespace {

sim::AgentSpec taker(std::string name, double cash = 1e6) {
    return sim::AgentSpec{
        .profile = {.name = std::move(name), .initial_cash = cash, .can_short = true},
        .timing = {},
        .behavior = mms::agents::InformedFlowSpec{.lambda = 4.0,
                                                  .direction_correlation = 0.0,
                                                  .size = sim::UniformIntSpec{.min = 1, .max = 3}}};
}

} // namespace

TEST_CASE("An agent acts once per decision period, counted from when it last acted",
          "[simulation][tick_loop][agents]") {
    sim::SimulationConfig config = make_config({.threshold = 0});
    config.agents[maker_id].timing.decision_period = 3.0;
    sim::World world(config);
    sim::TickLoop loop(world, config);

    loop.step(); // tick 1: the first act
    REQUIRE(loop.quotes_of(maker_id)[0].bid.has_value());
    const mm::OrderId first_bid = loop.quotes_of(maker_id)[0].bid->id;

    loop.step(); // ticks 2 and 3: not its turn, the quotes stay as they are
    loop.step();
    CHECK(loop.quotes_of(maker_id)[0].bid->id == first_bid);

    loop.step(); // tick 4: three units of time since tick 1, threshold 0 replaces
    CHECK(loop.quotes_of(maker_id)[0].bid->id != first_bid);
}

TEST_CASE("Another agent trades against the market maker and both accounts are settled",
          "[simulation][tick_loop][agents]") {
    sim::SimulationConfig config = make_config({.assets = 2});
    config.agents.push_back(taker("taker"));
    sim::World world(config);
    sim::TickLoop loop(world, config);
    Recorder recorder;
    loop.add_observer(recorder);

    for (int tick = 0; tick < 40; ++tick) {
        loop.step();
        const mm::PnlLedger& maker = world.agent(maker_id).account().ledger();
        const mm::PnlLedger& other = world.agent(2).account().ledger();
        for (mm::AssetId asset = 0; asset < 2; ++asset) {
            CHECK(maker.position(asset).quantity() + other.position(asset).quantity() == 0);
        }
        // Without fees the cash only changes hands.
        CHECK(world.agent(maker_id).account().cash() + world.agent(2).account().cash() ==
              Catch::Approx(1e12 + 1e6).epsilon(1e-12));
        check_consistent(world, loop);
    }

    REQUIRE_FALSE(recorder.trades.empty());
    for (const Recorder::TradeSeen& seen : recorder.trades) {
        CHECK(seen.parties.aggressive() != maker_id); // the taker started every one of them
    }
    CHECK(world.agent(2).account().ledger().position(0).quantity() != 0);
}

TEST_CASE("Orders the gateway refuses never reach an exchange", "[simulation][tick_loop][agents]") {
    sim::SimulationConfig config = make_config();
    config.agents = {taker("broke", 0.0)};
    sim::World world(config);
    sim::TickLoop loop(world, config);

    for (int tick = 0; tick < 20; ++tick) {
        loop.step();
    }

    // No cash to buy with and none to put up as collateral for a short sale.
    CHECK(world.agent(0).orders().size() == 0);
    CHECK(world.agent(0).account().pending_count() == 0);
    CHECK(world.matching_engine(0).book().empty(mm::Side::Buy));
    CHECK(world.matching_engine(0).book().empty(mm::Side::Sell));
}

TEST_CASE("Without a market maker the run goes on and an agent has no quotes",
          "[simulation][tick_loop][agents]") {
    sim::SimulationConfig config = make_config({.assets = 2});
    config.agents = {taker("alone")};
    sim::World world(config);
    sim::TickLoop loop(world, config);
    Recorder recorder;
    loop.add_observer(recorder);

    for (int tick = 0; tick < 10; ++tick) {
        loop.step();
    }

    const mm::LiveQuoteSet live = loop.quotes_of(0);
    REQUIRE(live.size() == 2);
    CHECK_FALSE(live[0].bid.has_value());
    CHECK_FALSE(live[1].ask.has_value());
    CHECK(recorder.trades.empty()); // nobody to trade with
}

TEST_CASE("Adding an agent does not change what another agent decides",
          "[simulation][tick_loop][agents]") {
    const auto summary = [](bool with_taker) {
        sim::SimulationConfig config = make_config({.mu = 0.5});
        if (with_taker) {
            // Too poor to do anything: it only has to be there.
            config.agents.push_back(taker("bystander", 0.0));
        }
        sim::World world(config);
        sim::TickLoop loop(world, config);
        std::vector<std::uint64_t> ids;
        for (int tick = 0; tick < 30; ++tick) {
            loop.step();
            const mm::LiveQuoteSet live = loop.quotes_of(maker_id);
            ids.push_back(live[0].bid.has_value() ? static_cast<std::uint64_t>(live[0].bid->id)
                                                  : 0U);
        }
        return ids;
    };

    CHECK(summary(false) == summary(true));
}

// ------------------------------------------------- orders from a scripted agent

namespace {

sim::SimulationConfig duel_config() {
    sim::SimulationConfig config = make_config();
    config.agents = {sim::tests::maker_agent(strategy_for(1), 1, {}, 1.0, "first"),
                     sim::tests::maker_agent(strategy_for(1), 1, {}, 1.0, "second")};
    return config;
}

/// Replaces the runtime of every agent of the world with one around a Scripted agent, and
/// returns the scripts in the order of the agents.
std::vector<sim::tests::Scripted*> install_scripts(sim::World& world,
                                                   const sim::SimulationConfig& config) {
    std::vector<mm::LedgerAsset> ledger_assets;
    for (const sim::AssetSpec& asset : config.assets) {
        ledger_assets.push_back({.initial_mid = asset.initial_mid});
    }
    const math::rng::RngHub hub(config.master_seed);
    std::vector<sim::tests::Scripted*> scripts;
    for (sim::AgentId id = 0; id < config.agents.size(); ++id) {
        auto agent = std::make_unique<sim::tests::Scripted>();
        scripts.push_back(agent.get());
        const math::rng::AssetKey key = math::rng::asset_key_of(config.agents[id].profile.name);
        world.agent(id) = sim::AgentRuntime(
            id,
            config.agents[id],
            config.account_rules,
            ledger_assets,
            hub.engine_for({.subsystem = math::rng::Subsystem::Agent, .asset = key}),
            hub.engine_for({.subsystem = math::rng::Subsystem::AgentLatency, .asset = key}),
            std::move(agent));
    }
    return scripts;
}

/// A world of one asset and two agents whose behavior the test writes: the runtimes the
/// world built are replaced by ones around Scripted agents. Prices barely move, unless the
/// config says otherwise.
struct Duel {
    explicit Duel(sim::SimulationConfig given = duel_config()) : config(std::move(given)) {}

    /// One tick in which `who` asks for `actions` and the other agent for nothing.
    void step_with(sim::AgentId who, std::vector<mms::agents::Action> actions) {
        script.at(0)->to_ask.clear();
        script.at(1)->to_ask.clear();
        script.at(who)->to_ask = std::move(actions);
        loop.step();
    }

    sim::SimulationConfig config;
    sim::World world{config};
    std::vector<sim::tests::Scripted*> script = install_scripts(world, config);
    sim::TickLoop loop{world, config};
};

mms::agents::SubmitLimit limit(mm::Side side,
                               mm::Price price,
                               mm::Quantity quantity,
                               mm::TimeInForce tif = mm::TimeInForce::GTC) {
    return mms::agents::SubmitLimit{
        .asset = 0, .side = side, .price = price, .quantity = quantity, .time_in_force = tif};
}

mm::Quantity position_of(const Duel& duel, sim::AgentId id) {
    return duel.world.agent(id).account().ledger().position(0).quantity();
}

} // namespace

TEST_CASE("An aggressive limit order trades with another agent quote and rests what is left",
          "[simulation][tick_loop][scripted]") {
    Duel duel;
    duel.step_with(0, {limit(mm::Side::Sell, 100.5, 10)});
    REQUIRE(duel.loop.recorded_orders() == 1);

    duel.step_with(1, {limit(mm::Side::Buy, 101.0, 15)});

    // 10 traded at the price of the resting order, 5 rest at the buyer's own price.
    CHECK(position_of(duel, 0) == -10);
    CHECK(position_of(duel, 1) == 10);
    CHECK(duel.world.agent(0).account().cash() == 1e12 + 1005.0);
    CHECK(duel.world.agent(1).account().cash() == 1e12 - 1005.0);
    CHECK(duel.world.agent(0).orders().size() == 0);
    REQUIRE(duel.world.agent(1).orders().size() == 1);
    const mms::agents::OpenOrder& left = duel.world.agent(1).orders().orders()[0];
    CHECK(left.state == mms::agents::OrderState::Resting);
    CHECK(left.remaining == 5);
    CHECK(left.price == 101.0);
    CHECK(duel.world.agent(1).account().pending_buy(0) == 5);
    CHECK(duel.world.matching_engine(0).book().empty(mm::Side::Sell));
    CHECK(duel.world.matching_engine(0).book().depth_at(mm::Side::Buy, 101.0) == 5);
    CHECK(duel.loop.recorded_orders() == 1);

    // Each side was told, and which of them started it.
    REQUIRE(duel.script[0]->fills.size() == 1);
    CHECK_FALSE(duel.script[0]->fills[0].aggressor);
    CHECK(duel.script[0]->fills[0].side == mm::Side::Sell);
    CHECK(duel.script[0]->fills[0].price == 100.5);
    CHECK(duel.script[0]->fills[0].quantity == 10);
    REQUIRE(duel.script[1]->fills.size() == 1);
    CHECK(duel.script[1]->fills[0].aggressor);
    CHECK(duel.script[1]->fills[0].side == mm::Side::Buy);
    CHECK(duel.script[1]->fills[0].quantity == 10);
}

TEST_CASE("An aggressive limit order that trades in full leaves nothing behind",
          "[simulation][tick_loop][scripted]") {
    Duel duel;
    duel.step_with(0, {limit(mm::Side::Sell, 100.5, 10)});

    duel.step_with(1, {limit(mm::Side::Buy, 101.0, 10)});

    CHECK(position_of(duel, 1) == 10);
    CHECK(duel.world.agent(0).orders().size() == 0);
    CHECK(duel.world.agent(1).orders().size() == 0);
    CHECK(duel.world.agent(1).account().pending_count() == 0);
    CHECK(duel.loop.recorded_orders() == 0);
    CHECK(duel.world.matching_engine(0).book().empty(mm::Side::Buy));
    CHECK(duel.world.matching_engine(0).book().empty(mm::Side::Sell));
}

TEST_CASE("An immediate limit order never rests, and what it held back is freed",
          "[simulation][tick_loop][scripted]") {
    Duel duel;
    duel.step_with(0, {limit(mm::Side::Sell, 100.5, 10)});

    duel.step_with(1, {limit(mm::Side::Buy, 101.0, 15, mm::TimeInForce::IOC)});

    CHECK(position_of(duel, 1) == 10);
    CHECK(duel.world.agent(1).orders().size() == 0);
    CHECK(duel.world.agent(1).account().pending_count() == 0);
    CHECK(duel.world.agent(1).account().buying_power() == duel.world.agent(1).account().cash());
    CHECK(duel.world.matching_engine(0).book().empty(mm::Side::Buy));
    CHECK(duel.loop.recorded_orders() == 0);
}

TEST_CASE("A cancel closes the order it names and frees what it held back",
          "[simulation][tick_loop][scripted]") {
    Duel duel;
    duel.step_with(1, {limit(mm::Side::Buy, 99.0, 5)});
    duel.step_with(1, {}); // the agent is shown the id of its resting order
    REQUIRE(duel.script[1]->last_orders.size() == 1);
    const mm::OrderId id = duel.script[1]->last_orders[0].id;
    REQUIRE(duel.world.agent(1).account().buying_power() < duel.world.agent(1).account().cash());

    duel.step_with(1, {mms::agents::Cancel{.asset = 0, .id = id}});

    CHECK(duel.world.agent(1).orders().size() == 0);
    CHECK(duel.world.agent(1).account().pending_count() == 0);
    CHECK(duel.world.agent(1).account().buying_power() == duel.world.agent(1).account().cash());
    CHECK(duel.world.matching_engine(0).book().empty(mm::Side::Buy));
    CHECK(duel.loop.recorded_orders() == 0);
}

TEST_CASE("An order that would cross the agent own resting one cancels the older instead",
          "[simulation][tick_loop][scripted]") {
    Duel duel;
    duel.step_with(0, {limit(mm::Side::Buy, 99.5, 5)});

    duel.step_with(0, {limit(mm::Side::Sell, 99.0, 5)});

    CHECK(position_of(duel, 0) == 0); // no trade with itself
    REQUIRE(duel.world.agent(0).orders().size() == 1);
    CHECK(duel.world.agent(0).orders().orders()[0].side == mm::Side::Sell);
    CHECK(duel.world.matching_engine(0).book().empty(mm::Side::Buy));
    CHECK(duel.world.agent(0).account().pending_buy(0) == 0); // the bid reserve is freed
    CHECK(duel.script[0]->fills.empty());
    CHECK(duel.loop.recorded_orders() == 1);
}

TEST_CASE("Another agent resting order is not touched by self-trade prevention",
          "[simulation][tick_loop][scripted]") {
    Duel duel;
    duel.step_with(1, {limit(mm::Side::Buy, 99.5, 5)});

    duel.step_with(0, {limit(mm::Side::Sell, 99.0, 5)}); // trades with the other agent bid

    CHECK(position_of(duel, 0) == -5);
    CHECK(position_of(duel, 1) == 5);
    CHECK(duel.loop.recorded_orders() == 0);
}

TEST_CASE("A trade is the market maker's when the market maker's order started it",
          "[simulation][tick_loop][scripted]") {
    // In a Duel the first agent is the observed market maker.
    Duel duel;
    Recorder recorder;
    duel.loop.add_observer(recorder);

    duel.step_with(1, {limit(mm::Side::Sell, 100.5, 10)});
    duel.step_with(0, {limit(mm::Side::Buy, 101.0, 4)}); // the maker takes
    duel.step_with(0, {limit(mm::Side::Buy, 99.0, 5)});
    duel.step_with(1, {limit(mm::Side::Sell, 99.0, 2)}); // the other takes

    REQUIRE(recorder.trades.size() == 2);
    CHECK(recorder.trades[0].parties.aggressive() == 0);
    CHECK(recorder.trades[1].parties.aggressive() == 1);
}

// -------------------------------------------------------- what an agent is shown

TEST_CASE("An agent is shown the tick size of the exchange, and the true price only if it is "
          "meant to have it",
          "[simulation][tick_loop][view]") {
    sim::SimulationConfig config = duel_config();
    config.assets[0].tick_size = 0.05;
    // The second agent is a trend follower: no signal for it.
    config.agents[1].behavior = mms::agents::TrendFollowerSpec{
        .half_life = 5.0, .threshold = 0.1, .order_size = 1, .max_position = 5};
    Duel duel(config);

    duel.loop.step();

    REQUIRE(duel.script[0]->last_tick_sizes.size() == 1);
    CHECK(duel.script[0]->last_tick_sizes[0] == 0.05);
    CHECK(duel.script[1]->last_tick_sizes[0] == 0.05);
    REQUIRE(duel.script[0]->last_signals[0].has_value());
    CHECK(*duel.script[0]->last_signals[0] == Catch::Approx(100.0).margin(1e-6));
    CHECK_FALSE(duel.script[1]->last_signals[0].has_value());
}

TEST_CASE("An agent is shown the best levels of the book, each with its total quantity",
          "[simulation][tick_loop][view]") {
    Duel duel;
    duel.step_with(0,
                   {limit(mm::Side::Buy, 99.0, 5),
                    limit(mm::Side::Buy, 99.0, 2),
                    limit(mm::Side::Buy, 98.0, 4),
                    limit(mm::Side::Sell, 101.0, 7)});

    duel.step_with(1, {}); // the second agent looks, at the start of the tick

    const auto& bids = duel.script[1]->last_bids[0];
    REQUIRE(bids.size() == 2);
    CHECK(bids[0].price == 99.0); // best first
    CHECK(bids[0].quantity == 7);
    CHECK(bids[1].price == 98.0);
    CHECK(bids[1].quantity == 4);
    const auto& asks = duel.script[1]->last_asks[0];
    REQUIRE(asks.size() == 1);
    CHECK(asks[0].price == 101.0);
    CHECK(asks[0].quantity == 7);
}

TEST_CASE("No more levels are shown than book_depth", "[simulation][tick_loop][view]") {
    sim::SimulationConfig config = duel_config();
    config.book_depth = 2;
    Duel duel(config);
    duel.step_with(0,
                   {limit(mm::Side::Buy, 99.0, 1),
                    limit(mm::Side::Buy, 98.0, 1),
                    limit(mm::Side::Buy, 97.0, 1)});

    duel.step_with(1, {});

    const auto& bids = duel.script[1]->last_bids[0];
    REQUIRE(bids.size() == 2);
    CHECK(bids[1].price == 98.0);
}

TEST_CASE("An agent is shown the trades since it last looked, once, with the side that started "
          "them",
          "[simulation][tick_loop][view]") {
    sim::SimulationConfig config = duel_config();
    config.agents[0].timing.decision_period = 3.0; // it looks at ticks 1, 4 and 7
    Duel duel(config);
    duel.step_with(0, {limit(mm::Side::Sell, 100.5, 10)});
    duel.step_with(1, {limit(mm::Side::Buy, 101.0, 4)}); // a trade of 4, started by a buyer
    duel.step_with(1, {limit(mm::Side::Buy, 101.0, 3)}); // and one of 3

    // The first agent has not looked since tick 1: it is shown both, oldest first.
    duel.step_with(0, {});
    const auto& seen = duel.script[0]->last_trades[0];
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].price == 100.5);
    CHECK(seen[0].quantity == 4);
    CHECK(seen[0].aggressor == mm::Side::Buy);
    CHECK(seen[1].quantity == 3);

    duel.step_with(1, {});
    duel.step_with(1, {});
    duel.step_with(0, {}); // tick 7: nothing happened since
    CHECK(duel.script[0]->last_tick == 7);
    CHECK(duel.script[0]->last_trades[0].empty());
}

TEST_CASE("An information delay shows the market as it was, and nothing else is late",
          "[simulation][tick_loop][view]") {
    sim::SimulationConfig config = duel_config();
    config.assets[0].mu = 5.0; // the price rises by 5 per unit of time
    config.agents[1].timing.info_delay = sim::ConstantDelaySpec{.delay = 2.0};
    Duel duel(config);
    // Prices barely move: volatility is 1e-9, so the price at time t is 100 + 5 t.

    for (int tick = 0; tick < 4; ++tick) {
        duel.loop.step();
    }

    REQUIRE(duel.script[0]->last_signals[0].has_value());
    CHECK(*duel.script[0]->last_signals[0] == Catch::Approx(120.0).margin(1e-5)); // time 4
    REQUIRE(duel.script[1]->last_signals[0].has_value());
    CHECK(*duel.script[1]->last_signals[0] == Catch::Approx(110.0).margin(1e-5)); // time 2
    // Its account and its clock are current.
    CHECK(duel.script[1]->last_time == 4.0);
    CHECK(duel.script[1]->last_tick == 4);
}

TEST_CASE("An information delay shows the book as it was and the trades up to then",
          "[simulation][tick_loop][view]") {
    sim::SimulationConfig config = duel_config();
    config.agents[1].timing.info_delay = sim::ConstantDelaySpec{.delay = 2.0};
    Duel duel(config);

    duel.step_with(0, {limit(mm::Side::Sell, 100.5, 10)}); // tick 1
    duel.step_with(0, {limit(mm::Side::Buy, 101.0, 4)});   // tick 2: trades with its own? no: STP
    duel.step_with(1, {});                                 // tick 3: the delayed agent looks

    // At tick 3 it is shown the market of time 1: before anything had been posted.
    CHECK(duel.script[1]->last_asks[0].empty());

    duel.step_with(1, {}); // tick 4: it is shown time 2, where the ask rests
    REQUIRE(duel.script[1]->last_asks[0].size() == 1);
    CHECK(duel.script[1]->last_asks[0][0].quantity == 10);
}

TEST_CASE("A random information delay is drawn from the agent's own stream, so it replays",
          "[simulation][tick_loop][view]") {
    const auto signals = [](std::uint64_t seed) {
        sim::SimulationConfig config = duel_config();
        config.master_seed = math::rng::Seed{seed};
        config.assets[0].mu = 5.0;
        config.agents[1].timing.info_delay = sim::UniformDelaySpec{.min = 0.0, .max = 5.0};
        Duel duel(config);
        std::vector<double> seen;
        for (int tick = 0; tick < 20; ++tick) {
            duel.loop.step();
            seen.push_back(*duel.script[1]->last_signals[0]);
        }
        return seen;
    };

    CHECK(signals(3) == signals(3));
    CHECK(signals(3) != signals(4));
    for (const double price : signals(3)) {
        CHECK(price <= 100.0 + (5.0 * 20.0) + 1e-5);
    }
}

TEST_CASE("The common factors go to the factor traders, added up between two acts",
          "[simulation][tick_loop][view]") {
    sim::SimulationConfig config = duel_config();
    config.process = sim::FactorModelSpec{.factor_count = 2};
    config.assets[0].factor_model = sim::FactorModelAssetData{
        .beta = (math::Vector(2) << 1.0, 0.5).finished(), .idiosyncratic_volatility = 0.1};
    config.assets[0].price_volatility = 1.0;
    // The first agent is a factor trader (acting every tick), the second acts every 3 ticks.
    config.agents[0].behavior = mms::agents::FactorTraderSpec{
        .sensitivities = {1.0, 1.0}, .threshold = 0.0, .order_size = 1, .max_position = 5};
    config.agents[1].behavior = mms::agents::FactorTraderSpec{
        .sensitivities = {1.0, 1.0}, .threshold = 0.0, .order_size = 1, .max_position = 5};
    config.agents[1].timing.decision_period = 3.0;
    Duel duel(config);

    std::vector<double> every_tick;
    std::vector<double> since_last(2, 0.0);
    for (int tick = 1; tick <= 7; ++tick) {
        duel.loop.step();
        const math::Vector& moves = duel.world.price_process().factor_increments();
        REQUIRE(moves.size() == 2);
        // The first agent acts every tick: it is shown exactly this tick's moves.
        CHECK(duel.script[0]->last_factors == std::vector<double>{moves(0), moves(1)});
        since_last[0] += moves(0);
        since_last[1] += moves(1);
        if (duel.script[1]->last_tick == tick) {
            // The second one acted this tick: the moves of all the ticks since its last act.
            CHECK(duel.script[1]->last_factors[0] == Catch::Approx(since_last[0]).margin(1e-12));
            CHECK(duel.script[1]->last_factors[1] == Catch::Approx(since_last[1]).margin(1e-12));
            since_last.assign(2, 0.0);
        }
    }
    CHECK(duel.script[1]->acts == 3); // ticks 1, 4 and 7
}

TEST_CASE("Agents that are not factor traders are shown no factors",
          "[simulation][tick_loop][view]") {
    sim::SimulationConfig config = duel_config();
    config.process = sim::FactorModelSpec{.factor_count = 1};
    config.assets[0].factor_model = sim::FactorModelAssetData{
        .beta = (math::Vector(1) << 1.0).finished(), .idiosyncratic_volatility = 0.1};
    config.assets[0].price_volatility = 1.0;
    Duel duel(config);

    duel.loop.step();
    duel.loop.step();

    CHECK(duel.script[0]->last_factors.empty());
    CHECK(duel.script[1]->last_factors.empty());
}

TEST_CASE("The snapshots kept reach as far back as the longest information delay",
          "[simulation][tick_loop][view]") {
    // The price rises by 5 per unit of time, so how old the signal is can be read off it.
    const auto staleness = [](sim::DelaySpec delay, int ticks) {
        sim::SimulationConfig config = duel_config();
        config.assets[0].mu = 5.0;
        config.agents[1].timing.info_delay = delay;
        Duel duel(config);
        double oldest = 0.0;
        for (int tick = 1; tick <= ticks; ++tick) {
            duel.loop.step();
            const double age = (100.0 + (5.0 * tick) - *duel.script[1]->last_signals[0]) / 5.0;
            oldest = std::max(oldest, age);
        }
        return oldest;
    };

    // A uniform delay is stale by up to its maximum, and not more.
    const double uniform = staleness(sim::UniformDelaySpec{.min = 0.0, .max = 3.0}, 100);
    CHECK(uniform > 2.0);
    CHECK(uniform <= 3.0 + 1e-6);

    // An exponential delay has no maximum: a long one is still shown as old as it was drawn.
    CHECK(staleness(sim::ExponentialDelaySpec{.mean = 1.0}, 300) > 2.5);
}

TEST_CASE("Delayed and infrequent: every trade is shown once, none is lost",
          "[simulation][tick_loop][view]") {
    sim::SimulationConfig config = duel_config();
    config.agents.push_back(sim::tests::maker_agent(strategy_for(1), 1, {}, 1.0, "taker"));
    config.agents[1].timing.decision_period = 3.0;
    config.agents[1].timing.info_delay = sim::ConstantDelaySpec{.delay = 2.0};
    Duel duel(config);
    // Agent 0 offers a lot at tick 1; agent 2 buys one at each tick from tick 2.
    duel.script[0]->to_ask = {limit(mm::Side::Sell, 100.5, 1000)};
    duel.script[0]->once = true;
    duel.script[2]->once = false;

    std::size_t shown = 0;
    for (int tick = 1; tick <= 19; ++tick) {
        duel.script[2]->to_ask.clear();
        if (tick >= 2) {
            duel.script[2]->to_ask = {
                mms::agents::SubmitMarket{.asset = 0, .side = mm::Side::Buy, .quantity = 1}};
        }
        const int acts_before = duel.script[1]->acts;
        duel.loop.step();
        if (duel.script[1]->acts > acts_before) {
            shown += duel.script[1]->last_trades[0].size();
        }
    }

    // The observer acts at ticks 1, 4, ..., 19 and sees the market of two ticks earlier: at
    // tick 19 that is the start of tick 17, which holds the trades of ticks 2 to 16.
    CHECK(duel.script[1]->acts == 7);
    CHECK(shown == 15);
}

TEST_CASE("A trade is shown with the side of the order that started it, buy or sell",
          "[simulation][tick_loop][view]") {
    Duel duel;
    duel.step_with(0, {limit(mm::Side::Buy, 99.0, 10)});
    duel.step_with(1, {limit(mm::Side::Sell, 98.5, 3)}); // a seller hits the bid
    duel.step_with(0, {});

    REQUIRE(duel.script[0]->last_trades[0].size() == 1);
    CHECK(duel.script[0]->last_trades[0][0].aggressor == mm::Side::Sell);
    CHECK(duel.script[0]->last_trades[0][0].price == 99.0);
}

// ------------------------------------------------------ what observers are told

namespace {

/// Keeps the refusals a run notifies, in order.
struct RejectLog final : sim::SimulationObserver {
    struct Entry {
        sim::AgentId agent;
        mms::agents::Action action;
        mms::agents::RejectReason reason;
    };

    std::vector<Entry> entries;
    std::vector<std::string> sequence;

    void on_tick_start(double /*time*/) override { sequence.emplace_back("start"); }

    void on_reject(sim::AgentId agent,
                   const mms::agents::Action& action,
                   mms::agents::RejectReason reason) override {
        sequence.emplace_back("reject");
        entries.push_back({.agent = agent, .action = action, .reason = reason});
    }

    void on_tick_end(double /*time*/) override { sequence.emplace_back("end"); }
};

} // namespace

TEST_CASE("Observers are told what the gateway refuses, with the agent and the reason",
          "[simulation][tick_loop][observers]") {
    Duel duel;
    RejectLog log;
    duel.loop.add_observer(log);

    duel.step_with(1, {limit(mm::Side::Buy, 99.0, 0), limit(mm::Side::Buy, 99.0, 3)});

    REQUIRE(log.entries.size() == 1);
    CHECK(log.entries[0].agent == 1);
    CHECK(log.entries[0].reason == mms::agents::RejectReason::InvalidOrder);
    CHECK(std::get<mms::agents::SubmitLimit>(log.entries[0].action).quantity == 0);
    // And it happens during the tick, where the agent acted.
    CHECK(log.sequence == std::vector<std::string>{"start", "reject", "end"});
    // The other order, a good one, went through.
    CHECK(duel.world.agent(1).orders().size() == 1);
}

TEST_CASE("Nothing is notified when nothing is refused", "[simulation][tick_loop][observers]") {
    Duel duel;
    RejectLog log;
    duel.loop.add_observer(log);

    duel.step_with(1, {limit(mm::Side::Buy, 99.0, 3)});

    CHECK(log.entries.empty());
}

TEST_CASE("The quotes of any agent can be read, and each agent has its own",
          "[simulation][tick_loop][observers]") {
    Duel duel;
    duel.step_with(1, {limit(mm::Side::Buy, 99.0, 3), limit(mm::Side::Sell, 101.0, 5)});

    const mm::LiveQuoteSet theirs = duel.loop.quotes_of(1);
    REQUIRE(theirs.size() == 1);
    REQUIRE(theirs[0].bid.has_value());
    CHECK(theirs[0].bid->quote.price == 99.0);
    CHECK(theirs[0].bid->quote.quantity == 3);
    REQUIRE(theirs[0].ask.has_value());
    CHECK(theirs[0].ask->quote.quantity == 5);

    const mm::LiveQuoteSet mine = duel.loop.quotes_of(0);
    CHECK_FALSE(mine[0].bid.has_value());
    CHECK_FALSE(mine[0].ask.has_value());

    CHECK_THROWS_AS(duel.loop.quotes_of(2), std::out_of_range);
}
