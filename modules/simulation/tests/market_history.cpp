/// \file
/// Tests for simulation/market_history.hpp: which snapshot an agent is shown for a given delay,
/// what is kept and for how long, and which trades it is shown, once each.

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mm/types.hpp>

#include <simulation/market_history.hpp>

namespace sim = mms::simulation;
namespace agents = mms::agents;
namespace mm = mms::mm;

namespace {

using Book = std::vector<std::vector<agents::BookLevel>>;

/// One level per side of each of two assets, priced off `base` so a snapshot is recognisable.
void record_at(sim::MarketHistory& history, double time, double base) {
    const std::vector<double> prices = {base, base + 1.0};
    Book bids = {{{.price = base - 1.0, .quantity = 3}}, {{.price = base, .quantity = 4}}};
    Book asks = {{{.price = base + 1.0, .quantity = 5}}, {{.price = base + 2.0, .quantity = 6}}};
    history.record(time, prices, std::move(bids), std::move(asks));
}

sim::MarketHistory
make_history(std::size_t snapshots = 10, std::size_t tape = 10, std::size_t limit = 50) {
    return sim::MarketHistory({0.01, 0.05}, snapshots, tape, limit);
}

agents::TradePrint print(double price, mm::Quantity quantity = 1) {
    return agents::TradePrint{.price = price, .quantity = quantity, .aggressor = mm::Side::Buy};
}

} // namespace

TEST_CASE("A tick size that is not finite and positive is refused",
          "[simulation][market_history]") {
    for (const double bad : {0.0,
                             -0.01,
                             std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        CHECK_THROWS_AS(sim::MarketHistory({0.01, bad}, 1, 1, 10), std::invalid_argument);
    }
}

TEST_CASE("Nothing can be viewed before something is recorded", "[simulation][market_history]") {
    sim::MarketHistory history = make_history();
    std::uint64_t seen = 0;

    CHECK_THROWS_AS(history.view(0.0, false, seen), std::logic_error);
}

TEST_CASE("A record must have one entry per asset", "[simulation][market_history]") {
    sim::MarketHistory history = make_history();

    CHECK_THROWS_AS(history.record(1.0, std::vector<double>{100.0}, Book(2), Book(2)),
                    std::invalid_argument);
    CHECK_THROWS_AS(history.record(1.0, std::vector<double>{100.0, 50.0}, Book(1), Book(2)),
                    std::invalid_argument);
    CHECK_THROWS_AS(history.record(1.0, std::vector<double>{100.0, 50.0}, Book(2), Book(3)),
                    std::invalid_argument);
    CHECK(history.snapshot_count() == 0);
}

TEST_CASE("The view shows the books, the tick sizes and, if asked, the true prices",
          "[simulation][market_history]") {
    sim::MarketHistory history = make_history();
    record_at(history, 1.0, 100.0);
    std::uint64_t seen = 0;

    const auto with_signal = history.view(1.0, true, seen);
    REQUIRE(with_signal.size() == 2);
    REQUIRE(with_signal[0].bids.size() == 1);
    CHECK(with_signal[0].bids[0].price == 99.0);
    CHECK(with_signal[0].bids[0].quantity == 3);
    CHECK(with_signal[0].asks[0].price == 101.0);
    CHECK(with_signal[1].bids[0].quantity == 4);
    CHECK(with_signal[1].asks[0].quantity == 6);
    CHECK(with_signal[0].tick_size == 0.01);
    CHECK(with_signal[1].tick_size == 0.05);
    REQUIRE(with_signal[0].signal.has_value());
    CHECK(*with_signal[0].signal == 100.0);
    CHECK(*with_signal[1].signal == 101.0);

    const auto without = history.view(1.0, false, seen);
    CHECK_FALSE(without[0].signal.has_value());
    CHECK_FALSE(without[1].signal.has_value());
}

TEST_CASE("A delay shows the newest snapshot that is old enough", "[simulation][market_history]") {
    sim::MarketHistory history = make_history();
    record_at(history, 1.0, 100.0);
    record_at(history, 2.0, 200.0);
    record_at(history, 3.0, 300.0);
    std::uint64_t seen = 0;

    CHECK(*history.view(3.0, true, seen)[0].signal == 300.0); // no delay
    CHECK(*history.view(2.5, true, seen)[0].signal == 200.0); // between two snapshots
    CHECK(*history.view(2.0, true, seen)[0].signal == 200.0); // exactly on one
    CHECK(*history.view(1.0, true, seen)[0].signal == 100.0);
    CHECK(*history.view(10.0, true, seen)[0].signal == 300.0); // a time in the future: the latest
}

TEST_CASE("A delay older than anything kept shows the oldest snapshot",
          "[simulation][market_history]") {
    sim::MarketHistory history = make_history(2);
    record_at(history, 1.0, 100.0);
    record_at(history, 2.0, 200.0);
    record_at(history, 3.0, 300.0);
    std::uint64_t seen = 0;

    CHECK(history.snapshot_count() == 2);
    CHECK(*history.view(0.0, true, seen)[0].signal == 200.0); // 100 was let go
}

TEST_CASE("At least one snapshot and one tick of tape are kept, whatever is asked",
          "[simulation][market_history]") {
    sim::MarketHistory history({0.01, 0.05}, 0, 0, 10);
    record_at(history, 1.0, 100.0);
    record_at(history, 2.0, 200.0);

    CHECK(history.snapshot_count() == 1);
    CHECK(history.tape_ticks() == 1);
}

TEST_CASE("Trades join the tape at the next record, and are shown once",
          "[simulation][market_history]") {
    sim::MarketHistory history = make_history();
    std::uint64_t seen = 0;
    record_at(history, 1.0, 100.0);
    history.add_trade(0, print(100.0, 2));
    history.add_trade(0, print(100.5, 3));
    history.add_trade(1, print(50.0, 1));

    // Not on the tape yet: the tick they happened in has not been recorded.
    CHECK(history.view(1.0, false, seen)[0].recent_trades.empty());

    record_at(history, 2.0, 100.0);
    const auto first = history.view(2.0, false, seen);
    REQUIRE(first[0].recent_trades.size() == 2);
    CHECK(first[0].recent_trades[0].price == 100.0); // oldest first, most recent last
    CHECK(first[0].recent_trades[1].price == 100.5);
    CHECK(first[0].recent_trades[1].quantity == 3);
    CHECK(first[0].recent_trades[1].aggressor == mm::Side::Buy);
    REQUIRE(first[1].recent_trades.size() == 1);
    CHECK(first[1].recent_trades[0].price == 50.0);

    // Shown once: a second look at the same time has nothing new.
    const auto again = history.view(2.0, false, seen);
    CHECK(again[0].recent_trades.empty());
    CHECK(again[1].recent_trades.empty());

    history.add_trade(0, print(101.0));
    record_at(history, 3.0, 100.0);
    const auto later = history.view(3.0, false, seen);
    REQUIRE(later[0].recent_trades.size() == 1);
    CHECK(later[0].recent_trades[0].price == 101.0);
}

TEST_CASE("An agent that acts rarely is shown everything since it last looked",
          "[simulation][market_history]") {
    sim::MarketHistory history = make_history();
    std::uint64_t seen = 0;
    for (int tick = 1; tick <= 5; ++tick) {
        record_at(history, tick, 100.0);
        history.add_trade(0, print(100.0 + tick));
    }
    record_at(history, 6.0, 100.0);

    const auto view = history.view(6.0, false, seen);

    REQUIRE(view[0].recent_trades.size() == 5);
    CHECK(view[0].recent_trades.front().price == 101.0);
    CHECK(view[0].recent_trades.back().price == 105.0);
}

TEST_CASE("The trades shown are only the ones as old as the snapshot shown",
          "[simulation][market_history]") {
    sim::MarketHistory history = make_history();
    std::uint64_t seen = 0;
    record_at(history, 1.0, 100.0);
    history.add_trade(0, print(1.0));
    record_at(history, 2.0, 100.0); // the first trade is on this tick
    history.add_trade(0, print(2.0));
    record_at(history, 3.0, 100.0); // and the second on this one

    const auto delayed = history.view(2.0, false, seen);
    REQUIRE(delayed[0].recent_trades.size() == 1);
    CHECK(delayed[0].recent_trades[0].price == 1.0);

    const auto current = history.view(3.0, false, seen);
    REQUIRE(current[0].recent_trades.size() == 1);
    CHECK(current[0].recent_trades[0].price == 2.0);
}

TEST_CASE("Looking at an older snapshot afterwards does not show trades again",
          "[simulation][market_history]") {
    sim::MarketHistory history = make_history();
    std::uint64_t seen = 0;
    record_at(history, 1.0, 100.0);
    history.add_trade(0, print(1.0));
    record_at(history, 2.0, 100.0);

    CHECK(history.view(2.0, false, seen)[0].recent_trades.size() == 1);
    CHECK(history.view(1.0, false, seen)[0].recent_trades.empty());
    CHECK(history.view(2.0, false, seen)[0].recent_trades.empty());
}

TEST_CASE("Each agent has its own place on the tape", "[simulation][market_history]") {
    sim::MarketHistory history = make_history();
    std::uint64_t one = 0;
    std::uint64_t other = 0;
    record_at(history, 1.0, 100.0);
    history.add_trade(0, print(1.0));
    record_at(history, 2.0, 100.0);

    CHECK(history.view(2.0, false, one)[0].recent_trades.size() == 1);
    CHECK(history.view(2.0, false, other)[0].recent_trades.size() == 1);
    CHECK(history.view(2.0, false, one)[0].recent_trades.empty());
}

TEST_CASE("No more than the limit is shown, the most recent ones", "[simulation][market_history]") {
    sim::MarketHistory history = make_history(10, 10, 3);
    std::uint64_t seen = 0;
    record_at(history, 1.0, 100.0);
    for (int i = 1; i <= 7; ++i) {
        history.add_trade(0, print(i));
    }
    record_at(history, 2.0, 100.0);

    const auto view = history.view(2.0, false, seen);

    REQUIRE(view[0].recent_trades.size() == 3);
    CHECK(view[0].recent_trades[0].price == 5.0);
    CHECK(view[0].recent_trades[2].price == 7.0);
}

TEST_CASE("A trade older than the tape is gone", "[simulation][market_history]") {
    sim::MarketHistory history = make_history(10, 2);
    std::uint64_t seen = 0;
    record_at(history, 1.0, 100.0);
    history.add_trade(0, print(1.0));
    record_at(history, 2.0, 100.0);
    record_at(history, 3.0, 100.0);
    record_at(history, 4.0, 100.0);

    CHECK(history.tape_ticks() == 2);
    CHECK(history.view(4.0, false, seen)[0].recent_trades.empty());
}

TEST_CASE("A limit of zero shows no trades", "[simulation][market_history]") {
    sim::MarketHistory history = make_history(10, 10, 0);
    std::uint64_t seen = 0;
    record_at(history, 1.0, 100.0);
    history.add_trade(0, print(1.0));
    record_at(history, 2.0, 100.0);

    CHECK(history.view(2.0, false, seen)[0].recent_trades.empty());
}
