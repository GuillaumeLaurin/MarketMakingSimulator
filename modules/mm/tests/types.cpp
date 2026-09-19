/// \file
/// Tests for mm/types.hpp: the concrete Price/Quantity/AssetId choices, that
/// every alias really is the matching core template instantiation, that the
/// re-exported core names are the core types themselves, to_double, and one
/// end-to-end run of the aliased MatchingEngine (a template body is only
/// checked once instantiated, so the aliases alone prove nothing about
/// whether core actually works with an integer Quantity).

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mm/types.hpp>

#include <core/core.hpp>

namespace core = mms::core;
namespace mm = mms::mm;

namespace {

struct TradeRecorder : mm::MatchingEngineObserver {
    std::vector<mm::Trade> trades;

    void on_trade(const mm::Trade& trade) override { trades.push_back(trade); }
};

} // namespace

TEST_CASE("Price, Quantity and AssetId are the chosen concrete types", "[mm][types]") {
    STATIC_REQUIRE(std::is_same_v<mm::Price, double>);
    STATIC_REQUIRE(std::is_same_v<mm::Quantity, std::int64_t>);
    STATIC_REQUIRE(std::is_same_v<mm::AssetId, std::size_t>);
}

TEST_CASE("Quantity is signed, so a short position is representable", "[mm][types]") {
    STATIC_REQUIRE(std::is_signed_v<mm::Quantity>);

    const mm::Quantity short_position = -5;
    CHECK(short_position < 0);
}

TEST_CASE("Price and Quantity satisfy core::OrderedArithmetic", "[mm][types]") {
    STATIC_REQUIRE(core::OrderedArithmetic<mm::Price>);
    STATIC_REQUIRE(core::OrderedArithmetic<mm::Quantity>);
}

TEST_CASE("Every alias is the matching core instantiation", "[mm][types]") {
    STATIC_REQUIRE(std::is_same_v<mm::LimitOrder, core::LimitOrder<double, std::int64_t>>);
    STATIC_REQUIRE(std::is_same_v<mm::MarketOrder, core::MarketOrder<std::int64_t>>);
    STATIC_REQUIRE(std::is_same_v<mm::Order, core::Order<double, std::int64_t>>);
    STATIC_REQUIRE(std::is_same_v<mm::Trade, core::Trade<double, std::int64_t>>);
    STATIC_REQUIRE(std::is_same_v<mm::OrderBook, core::OrderBook<double, std::int64_t>>);
    STATIC_REQUIRE(std::is_same_v<mm::MatchingEngine, core::MatchingEngine<double, std::int64_t>>);
    STATIC_REQUIRE(std::is_same_v<mm::MatchingEngineObserver,
                                  core::MatchingEngineObserver<double, std::int64_t>>);
}

TEST_CASE("The re-exported core names are the core types themselves", "[mm][types]") {
    STATIC_REQUIRE(std::is_same_v<mm::Side, core::Side>);
    STATIC_REQUIRE(std::is_same_v<mm::OrderId, core::OrderId>);
    STATIC_REQUIRE(std::is_same_v<mm::TimeInForce, core::TimeInForce>);
    STATIC_REQUIRE(std::is_same_v<mm::MarketTimeInForce, core::MarketTimeInForce>);
}

TEST_CASE("to_double converts a Quantity exactly", "[mm][types]") {
    STATIC_REQUIRE(mm::to_double(0) == 0.0);
    STATIC_REQUIRE(mm::to_double(42) == 42.0);
    STATIC_REQUIRE(mm::to_double(-7) == -7.0); // a short position keeps its sign
}

TEST_CASE("to_double is exact up to 2^53 in both directions", "[mm][types]") {
    constexpr mm::Quantity two_to_53 = mm::Quantity{1} << 53;

    STATIC_REQUIRE(mm::to_double(two_to_53) == 9007199254740992.0);
    STATIC_REQUIRE(static_cast<mm::Quantity>(mm::to_double(two_to_53)) == two_to_53);
    STATIC_REQUIRE(static_cast<mm::Quantity>(mm::to_double(-two_to_53)) == -two_to_53);
}

TEST_CASE("to_double is constexpr and noexcept", "[mm][types]") {
    STATIC_REQUIRE(noexcept(mm::to_double(1)));

    constexpr double converted = mm::to_double(10);
    STATIC_REQUIRE(converted == 10.0);
}

TEST_CASE("The aliased MatchingEngine matches with an integer Quantity end to end", "[mm][types]") {
    mm::MatchingEngine engine;
    TradeRecorder recorder;
    engine.add_observer(recorder);

    const mm::OrderId sell_id =
        engine.submit(mm::LimitOrder{mm::Side::Sell, 100.0, 10, mm::TimeInForce::GTC});
    static_cast<void>(engine.submit(mm::MarketOrder{mm::Side::Buy, 4, mm::MarketTimeInForce::IOC}));

    REQUIRE(recorder.trades.size() == 1);
    CHECK(recorder.trades[0].price == 100.0);
    CHECK(recorder.trades[0].quantity == 4);
    CHECK(recorder.trades[0].sell_order_id == sell_id);
    CHECK(engine.book().depth_at(mm::Side::Sell, 100.0) == 6);

    // The rest of the resting order is consumed: with integers the level
    // reaches exactly zero and disappears, no floating-point dust left.
    static_cast<void>(engine.submit(mm::MarketOrder{mm::Side::Buy, 6, mm::MarketTimeInForce::IOC}));

    REQUIRE(recorder.trades.size() == 2);
    CHECK(engine.book().empty(mm::Side::Sell));
    CHECK(engine.book().depth_at(mm::Side::Sell, 100.0) == 0);
}
