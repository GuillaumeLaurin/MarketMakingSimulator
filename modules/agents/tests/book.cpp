/// \file
/// Tests for agents/book.hpp. An agent that does not know the true price goes by the middle of
/// the book; with one side missing it goes by the last trade, and with neither it has no price.

#include <optional>
#include <vector>

#include <agents/book.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

namespace agents = mms::agents;
namespace mm = mms::mm;

agents::AssetView view(std::vector<agents::BookLevel>& bids,
                       std::vector<agents::BookLevel>& asks,
                       std::vector<agents::TradePrint>& trades) {
    return agents::AssetView{
        .bids = bids, .asks = asks, .recent_trades = trades, .signal = std::nullopt};
}

} // namespace

TEST_CASE("The middle of the book is halfway between the best bid and the best ask",
          "[agents][book]") {
    std::vector<agents::BookLevel> bids{{99.0, 5}, {98.0, 9}};
    std::vector<agents::BookLevel> asks{{101.0, 5}, {103.0, 9}};
    std::vector<agents::TradePrint> trades;

    const auto mid = agents::book_mid(view(bids, asks, trades));

    REQUIRE(mid.has_value());
    CHECK(*mid == 100.0); // the best of each side, not the deeper levels
}

TEST_CASE("A two-sided book is read from the book even when there are trades", "[agents][book]") {
    std::vector<agents::BookLevel> bids{{99.0, 5}};
    std::vector<agents::BookLevel> asks{{101.0, 5}};
    std::vector<agents::TradePrint> trades{{150.0, 1, mm::Side::Buy}};

    CHECK(*agents::book_mid(view(bids, asks, trades)) == 100.0);
}

TEST_CASE("With one side empty the last trade is the price", "[agents][book]") {
    std::vector<agents::BookLevel> bids{{99.0, 5}};
    std::vector<agents::BookLevel> none;
    std::vector<agents::TradePrint> trades{{50.0, 1, mm::Side::Sell}, {52.5, 2, mm::Side::Buy}};

    const auto only_bids = agents::book_mid(view(bids, none, trades));
    const auto only_asks = agents::book_mid(view(none, bids, trades));

    REQUIRE(only_bids.has_value());
    REQUIRE(only_asks.has_value());
    CHECK(*only_bids == 52.5); // the most recent trade is the last one
    CHECK(*only_asks == 52.5);
}

TEST_CASE("With an empty book the last trade is still the price", "[agents][book]") {
    std::vector<agents::BookLevel> none;
    std::vector<agents::TradePrint> trades{{77.0, 3, mm::Side::Buy}};

    CHECK(*agents::book_mid(view(none, none, trades)) == 77.0);
}

TEST_CASE("With no book and no trade there is no price", "[agents][book]") {
    std::vector<agents::BookLevel> none;
    std::vector<agents::BookLevel> bids{{99.0, 5}};
    std::vector<agents::TradePrint> no_trades;

    CHECK_FALSE(agents::book_mid(view(none, none, no_trades)).has_value());
    CHECK_FALSE(agents::book_mid(view(bids, none, no_trades)).has_value());
    CHECK_FALSE(agents::book_mid(view(none, bids, no_trades)).has_value());
    CHECK_FALSE(agents::book_mid(agents::AssetView{}).has_value());
}
