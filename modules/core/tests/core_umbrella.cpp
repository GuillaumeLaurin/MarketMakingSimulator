/// \file
/// The <core/core.hpp> umbrella pulls in every public symbol of the core
/// module: one small end-to-end scenario touching each header.

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <core/core.hpp>

namespace {

struct TradeRecorder : mms::core::MatchingEngineObserver<double, double> {
    std::vector<mms::core::Trade<double, double>> trades;

    void on_trade(const mms::core::Trade<double, double>& trade) override {
        trades.push_back(trade);
    }
};

} // namespace

TEST_CASE("<core/core.hpp> exposes the whole core module", "[core][umbrella]") {
    using namespace mms::core;

    // concepts.hpp
    static_assert(OrderedArithmetic<double>);

    // side.hpp, order_id.hpp, order.hpp
    const OrderId resting_id = OrderId{0};
    const Order<double, double> sell = LimitOrder<double, double>{
        .side = Side::Sell, .price = 100.0, .quantity = 10.0, .time_in_force = TimeInForce::GTC};
    const Order<double, double> buy = MarketOrder<double>{
        .side = Side::Buy, .quantity = 4.0, .time_in_force = MarketTimeInForce::IOC};

    // matching_engine.hpp and order_book.hpp
    MatchingEngine<double, double> engine;
    TradeRecorder recorder;
    engine.add_observer(recorder);

    const OrderId sell_id = engine.submit(sell);
    CHECK(sell_id != resting_id);
    CHECK(engine.book().depth_at(Side::Sell, 100.0) == 10.0);

    static_cast<void>(engine.submit(buy));

    // trade.hpp
    REQUIRE(recorder.trades.size() == 1);
    CHECK(recorder.trades[0].price == 100.0);
    CHECK(recorder.trades[0].quantity == 4.0);
    CHECK(recorder.trades[0].sell_order_id == sell_id);
    CHECK(engine.book().depth_at(Side::Sell, 100.0) == 6.0);

    // latency_queue.hpp
    LatencyQueue<Order<double, double>> queue;
    queue.schedule(1.0, sell);
    CHECK(queue.drain_until(1.0).size() == 1);
}
