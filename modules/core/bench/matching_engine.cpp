/// \file
/// What the matching engine costs per operation, by book depth (resting orders
/// on each side, one per price level): resting an order and cancelling it, a
/// market order that takes the best level, and one that sweeps five levels.
/// The trading cases put back what they took so the book stays the same depth
/// from one iteration to the next, so their time includes that refill.
/// The same cases are run with 0, 1 and 3 observers attached, since every
/// event is reported to each of them through a virtual call.
/// Run on demand: build/<preset>/out/bench/core_matching_engine.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <nanobench.h>

#include <core/matching_engine.hpp>
#include <core/order.hpp>
#include <core/order_id.hpp>
#include <core/side.hpp>
#include <core/trade.hpp>

namespace {

namespace core = mms::core;
using Engine = core::MatchingEngine<double, std::int64_t>;
using Limit = core::LimitOrder<double, std::int64_t>;
using Market = core::MarketOrder<std::int64_t>;
using Trade = core::Trade<double, std::int64_t>;

constexpr double mid = 100.0;
constexpr double tick = 0.01;
constexpr std::int64_t level_size = 10;
constexpr std::size_t swept = 5;

double ask_price(std::size_t level) {
    return mid + (tick * static_cast<double>(level + 1));
}

double bid_price(std::size_t level) {
    return mid - (tick * static_cast<double>(level + 1));
}

Limit limit(core::Side side, double price, std::int64_t quantity = level_size) {
    return Limit{.side = side,
                 .price = price,
                 .quantity = quantity,
                 .time_in_force = core::TimeInForce::GTC};
}

// `depth` price levels on each side, one resting order of level_size each.
Engine make_engine(std::size_t depth) {
    Engine engine;
    for (std::size_t level = 0; level < depth; ++level) {
        engine.submit(limit(core::Side::Buy, bid_price(level)));
        engine.submit(limit(core::Side::Sell, ask_price(level)));
    }
    return engine;
}

// Does the little a real observer does at the very least: keeps a tally.
class CountingObserver final : public core::MatchingEngineObserver<double, std::int64_t> {
public:
    void on_order_added(core::OrderId /*id*/, const Limit& /*order*/) override { ++added_; }

    void on_order_cancelled(core::OrderId /*id*/) override { ++cancelled_; }

    void on_trade(const Trade& trade) override {
        ++trades_;
        traded_quantity_ += trade.quantity;
    }

    [[nodiscard]] std::uint64_t events() const noexcept { return added_ + cancelled_ + trades_; }

private:
    std::uint64_t added_ = 0;
    std::uint64_t cancelled_ = 0;
    std::uint64_t trades_ = 0;
    std::int64_t traded_quantity_ = 0;
};

void run_cases(ankerl::nanobench::Bench& bench, std::size_t depth, std::size_t observer_count) {
    const std::string at =
        ", depth " + std::to_string(depth) + ", " + std::to_string(observer_count) + " observers";
    std::array<CountingObserver, 3> observers;

    const auto engine_with_observers = [&](std::size_t book_depth) {
        Engine engine = make_engine(book_depth);
        for (std::size_t i = 0; i < observer_count; ++i) {
            engine.add_observer(observers[i]);
        }
        return engine;
    };

    {
        // A bid inside the spread: it rests without crossing.
        Engine engine = engine_with_observers(depth);
        bench.run("rest + cancel" + at, [&] {
            const auto id = engine.submit(limit(core::Side::Buy, mid - (tick * 0.5)));
            ankerl::nanobench::doNotOptimizeAway(engine.cancel(id));
        });
    }

    {
        // Take the best ask level, then rest a new ask above the others.
        Engine engine = engine_with_observers(depth);
        std::size_t next_level = depth;
        bench.run("market order, 1 level + refill" + at, [&] {
            engine.submit(Market{.side = core::Side::Buy,
                                 .quantity = level_size,
                                 .time_in_force = core::MarketTimeInForce::IOC});
            engine.submit(limit(core::Side::Sell, ask_price(next_level++)));
        });
    }

    {
        Engine engine = engine_with_observers(depth + swept);
        std::size_t next_level = depth + swept;
        bench.run("market order, 5 levels + refill" + at, [&] {
            engine.submit(Market{.side = core::Side::Buy,
                                 .quantity = level_size * swept,
                                 .time_in_force = core::MarketTimeInForce::IOC});
            for (std::size_t i = 0; i < swept; ++i) {
                engine.submit(limit(core::Side::Sell, ask_price(next_level++)));
            }
        });
    }

    for (const CountingObserver& observer : observers) {
        ankerl::nanobench::doNotOptimizeAway(observer.events());
    }
}

} // namespace

int main() {
    ankerl::nanobench::Bench bench;
    bench.title("matching engine").warmup(1000);

    for (const std::size_t depth : {10U, 100U, 1000U}) {
        run_cases(bench, depth, 0);
    }
    // Observers cost the same at any depth, so one depth is enough.
    for (const std::size_t observer_count : {1U, 3U}) {
        run_cases(bench, 100, observer_count);
    }
    return 0;
}
