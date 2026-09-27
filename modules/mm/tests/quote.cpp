/// \file
/// Tests for mm/quote.hpp: Quote and AssetQuotes equality (what requote and
/// the strategy tests rely on), the Buy/Sell routing of AssetQuotes::on,
/// the "empty side means no quote" default, and to_limit_order, whose whole
/// point is that a quote goes out as a resting Good-Till-Cancel order.

#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <mm/quote.hpp>
#include <mm/types.hpp>

namespace mm = mms::mm;

namespace {

mm::Quote quote(mm::Price price, mm::Quantity quantity) {
    return mm::Quote{.price = price, .quantity = quantity};
}

} // namespace

TEST_CASE("Quote equality compares both price and quantity", "[mm][quote]") {
    CHECK(quote(100.0, 10) == quote(100.0, 10));
    CHECK_FALSE(quote(100.0, 10) == quote(100.5, 10));
    CHECK_FALSE(quote(100.0, 10) == quote(100.0, 11));
}

TEST_CASE("A default AssetQuotes quotes nothing on either side", "[mm][quote]") {
    const mm::AssetQuotes quotes;

    CHECK_FALSE(quotes.bid.has_value());
    CHECK_FALSE(quotes.ask.has_value());
    CHECK_FALSE(quotes.on(mm::Side::Buy).has_value());
    CHECK_FALSE(quotes.on(mm::Side::Sell).has_value());
}

TEST_CASE("AssetQuotes::on routes Buy to the bid and Sell to the ask", "[mm][quote]") {
    const mm::AssetQuotes quotes{.bid = quote(99.0, 5), .ask = quote(101.0, 7)};

    CHECK(&quotes.on(mm::Side::Buy) == &quotes.bid);
    CHECK(&quotes.on(mm::Side::Sell) == &quotes.ask);
    CHECK(quotes.on(mm::Side::Buy) == quote(99.0, 5));
    CHECK(quotes.on(mm::Side::Sell) == quote(101.0, 7));
}

TEST_CASE("AssetQuotes::on gives write access to the matching side only", "[mm][quote]") {
    mm::AssetQuotes quotes;

    quotes.on(mm::Side::Sell) = quote(101.0, 7);

    CHECK_FALSE(quotes.bid.has_value()); // the bid is untouched
    REQUIRE(quotes.ask.has_value());
    CHECK(*quotes.ask == quote(101.0, 7));

    quotes.on(mm::Side::Buy) = quote(99.0, 5);
    quotes.on(mm::Side::Sell).reset(); // suppressing a side, as risk will do

    REQUIRE(quotes.bid.has_value());
    CHECK(*quotes.bid == quote(99.0, 5));
    CHECK_FALSE(quotes.ask.has_value());
}

TEST_CASE("AssetQuotes equality compares each side, and which side a quote is on", "[mm][quote]") {
    const mm::AssetQuotes base{.bid = quote(99.0, 5), .ask = quote(101.0, 7)};

    CHECK(base == mm::AssetQuotes{.bid = quote(99.0, 5), .ask = quote(101.0, 7)});
    CHECK(mm::AssetQuotes{} == mm::AssetQuotes{});

    CHECK_FALSE(base == mm::AssetQuotes{.bid = quote(98.0, 5), .ask = quote(101.0, 7)});
    CHECK_FALSE(base == mm::AssetQuotes{.bid = quote(99.0, 5), .ask = quote(102.0, 7)});
    CHECK_FALSE(base == mm::AssetQuotes{.bid = quote(99.0, 5), .ask = std::nullopt});
    CHECK_FALSE(base == mm::AssetQuotes{.bid = std::nullopt, .ask = quote(101.0, 7)});

    // The same quote as a bid is not the same as it being an ask.
    CHECK_FALSE(mm::AssetQuotes{.bid = quote(100.0, 5), .ask = std::nullopt} ==
                mm::AssetQuotes{.bid = std::nullopt, .ask = quote(100.0, 5)});
}

TEST_CASE("A QuoteSet holds one empty AssetQuotes per asset by default", "[mm][quote]") {
    const mm::QuoteSet quotes(3);

    REQUIRE(quotes.size() == 3);
    for (const mm::AssetQuotes& asset : quotes) {
        CHECK(asset == mm::AssetQuotes{});
    }
}

TEST_CASE("to_limit_order copies price and quantity and takes the requested side", "[mm][quote]") {
    const mm::LimitOrder bid = mm::to_limit_order(mm::Side::Buy, quote(99.5, 12));
    CHECK(bid.side == mm::Side::Buy);
    CHECK(bid.price == 99.5);
    CHECK(bid.quantity == 12);

    const mm::LimitOrder ask = mm::to_limit_order(mm::Side::Sell, quote(100.5, 8));
    CHECK(ask.side == mm::Side::Sell);
    CHECK(ask.price == 100.5);
    CHECK(ask.quantity == 8);
}

TEST_CASE("to_limit_order always produces a Good-Till-Cancel order", "[mm][quote]") {
    CHECK(mm::to_limit_order(mm::Side::Buy, quote(99.0, 1)).time_in_force == mm::TimeInForce::GTC);
    CHECK(mm::to_limit_order(mm::Side::Sell, quote(101.0, 1)).time_in_force ==
          mm::TimeInForce::GTC);
}

TEST_CASE("A quote sent to a MatchingEngine rests in the book", "[mm][quote]") {
    // The observable meaning of GTC: an IOC quote with nothing to match would
    // vanish, a GTC one rests at its price with its full size.
    mm::MatchingEngine engine;

    static_cast<void>(engine.submit(mm::to_limit_order(mm::Side::Buy, quote(99.0, 5))));
    static_cast<void>(engine.submit(mm::to_limit_order(mm::Side::Sell, quote(101.0, 7))));

    CHECK(engine.book().depth_at(mm::Side::Buy, 99.0) == 5);
    CHECK(engine.book().depth_at(mm::Side::Sell, 101.0) == 7);
}

TEST_CASE("AssetQuotes::on and to_limit_order are noexcept", "[mm][quote]") {
    STATIC_REQUIRE(noexcept(std::declval<mm::AssetQuotes&>().on(mm::Side::Buy)));
    STATIC_REQUIRE(noexcept(std::declval<const mm::AssetQuotes&>().on(mm::Side::Sell)));
    STATIC_REQUIRE(noexcept(mm::to_limit_order(mm::Side::Buy, std::declval<const mm::Quote&>())));
}
