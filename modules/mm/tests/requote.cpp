/// \file
/// Tests for mm/requote.hpp: needs_replacing (the tick threshold, its
/// boundary, the size trigger, and exactness on a decimal grid), then
/// plan_requote's four cases per side (live but not wanted, wanted but not
/// live, both and worth replacing, both and steady), independence of the two
/// sides and of the assets, per-asset tick sizes, deterministic ordering, and
/// validation.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mm/quote.hpp>
#include <mm/requote.hpp>
#include <mm/tick.hpp>
#include <mm/types.hpp>

namespace mm = mms::mm;

namespace {

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

mm::Quote quote(mm::Price price, mm::Quantity quantity) {
    return mm::Quote{.price = price, .quantity = quantity};
}

mm::LiveQuote live_quote(std::uint64_t id, mm::Price price, mm::Quantity quantity) {
    return mm::LiveQuote{.id = mm::OrderId{id}, .quote = quote(price, quantity)};
}

std::vector<mm::Price> half_tick() {
    return {0.5};
}

} // namespace

TEST_CASE("A quote identical to the resting one is replaced only at threshold 0", "[mm][requote]") {
    const mm::Quote resting = quote(100.0, 5);

    CHECK_FALSE(mm::needs_replacing(resting, quote(100.0, 5), 0.5, 1));
    CHECK(mm::needs_replacing(resting, quote(100.0, 5), 0.5, 0));
}

TEST_CASE("A price move triggers a replace from exactly the threshold in ticks", "[mm][requote]") {
    const mm::Quote resting = quote(100.0, 5);

    // 100.5 is one tick of 0.5 away, 101.0 is two.
    CHECK(mm::needs_replacing(resting, quote(100.5, 5), 0.5, 1));
    CHECK_FALSE(mm::needs_replacing(resting, quote(100.5, 5), 0.5, 2));
    CHECK(mm::needs_replacing(resting, quote(101.0, 5), 0.5, 2));
    CHECK_FALSE(mm::needs_replacing(resting, quote(101.0, 5), 0.5, 3));
}

TEST_CASE("The direction of the price move does not matter", "[mm][requote]") {
    const mm::Quote resting = quote(100.0, 5);

    CHECK(mm::needs_replacing(resting, quote(99.0, 5), 0.5, 2));
    CHECK_FALSE(mm::needs_replacing(resting, quote(99.5, 5), 0.5, 2));
}

TEST_CASE("A different size triggers a replace whatever the threshold", "[mm][requote]") {
    // A partial fill left 3 of 5 shares: the price is unchanged, the size is not.
    CHECK(mm::needs_replacing(quote(100.0, 3), quote(100.0, 5), 0.5, 1000));
    CHECK(mm::needs_replacing(quote(100.0, 5), quote(100.0, 3), 0.5, 1000));
}

TEST_CASE("Tick distance is exact on a decimal grid", "[mm][requote]") {
    // 100.3 and 100.4 are one tick of 0.1 apart, even though a naive
    // division of the difference by the tick is not exactly 1.
    CHECK(mm::needs_replacing(quote(100.3, 5), quote(100.4, 5), 0.1, 1));
    CHECK_FALSE(mm::needs_replacing(quote(100.3, 5), quote(100.4, 5), 0.1, 2));
    CHECK(mm::needs_replacing(quote(100.3, 5), quote(100.5, 5), 0.1, 2));
}

TEST_CASE("Nothing wanted and nothing live gives an empty plan", "[mm][requote]") {
    const mm::RequotePlan plan =
        mm::plan_requote(mm::QuoteSet(2), mm::LiveQuoteSet(2), {0.5, 0.5}, 1);

    CHECK(plan.cancels.empty());
    CHECK(plan.submits.empty());
}

TEST_CASE("Zero assets gives an empty plan", "[mm][requote]") {
    const mm::RequotePlan plan = mm::plan_requote({}, {}, {}, 1);

    CHECK(plan.cancels.empty());
    CHECK(plan.submits.empty());
}

TEST_CASE("A wanted quote with nothing live is submitted, bid first", "[mm][requote]") {
    mm::QuoteSet desired(1);
    desired[0].bid = quote(99.0, 5);
    desired[0].ask = quote(101.0, 7);

    const mm::RequotePlan plan = mm::plan_requote(desired, mm::LiveQuoteSet(1), half_tick(), 1);

    CHECK(plan.cancels.empty());
    REQUIRE(plan.submits.size() == 2);
    CHECK(plan.submits[0].asset == 0);
    CHECK(plan.submits[0].side == mm::Side::Buy);
    CHECK(plan.submits[0].quote == quote(99.0, 5));
    CHECK(plan.submits[1].asset == 0);
    CHECK(plan.submits[1].side == mm::Side::Sell);
    CHECK(plan.submits[1].quote == quote(101.0, 7));
}

TEST_CASE("A live quote that is no longer wanted is cancelled", "[mm][requote]") {
    // What happens when the risk limits suppress a side: the desired quote
    // is gone, the resting order must be pulled.
    mm::LiveQuoteSet live(1);
    live[0].bid = live_quote(11, 99.0, 5);
    live[0].ask = live_quote(12, 101.0, 5);

    const mm::RequotePlan plan = mm::plan_requote(mm::QuoteSet(1), live, half_tick(), 1);

    REQUIRE(plan.cancels.size() == 2);
    CHECK(plan.cancels[0].asset == 0);
    CHECK(plan.cancels[0].id == mm::OrderId{11});
    CHECK(plan.cancels[1].id == mm::OrderId{12});
    CHECK(plan.submits.empty());
}

TEST_CASE("A steady quote produces no action", "[mm][requote]") {
    mm::QuoteSet desired(1);
    desired[0].bid = quote(99.0, 5);
    desired[0].ask = quote(101.0, 5);
    mm::LiveQuoteSet live(1);
    live[0].bid = live_quote(1, 99.0, 5);
    live[0].ask = live_quote(2, 101.0, 5);

    const mm::RequotePlan plan = mm::plan_requote(desired, live, half_tick(), 1);

    CHECK(plan.cancels.empty());
    CHECK(plan.submits.empty());
}

TEST_CASE("A quote that moved by the threshold is cancelled and resubmitted", "[mm][requote]") {
    mm::QuoteSet desired(1);
    desired[0].bid = quote(100.0, 5); // was 99.0: two ticks up
    mm::LiveQuoteSet live(1);
    live[0].bid = live_quote(7, 99.0, 5);

    const mm::RequotePlan plan = mm::plan_requote(desired, live, half_tick(), 2);

    REQUIRE(plan.cancels.size() == 1);
    CHECK(plan.cancels[0].id == mm::OrderId{7});
    REQUIRE(plan.submits.size() == 1);
    CHECK(plan.submits[0].side == mm::Side::Buy);
    CHECK(plan.submits[0].quote == quote(100.0, 5)); // the wanted one, not the old
}

TEST_CASE("A quote that moved less than the threshold is left alone", "[mm][requote]") {
    mm::QuoteSet desired(1);
    desired[0].bid = quote(99.5, 5); // one tick from 99.0
    mm::LiveQuoteSet live(1);
    live[0].bid = live_quote(7, 99.0, 5);

    const mm::RequotePlan plan = mm::plan_requote(desired, live, half_tick(), 2);

    CHECK(plan.cancels.empty());
    CHECK(plan.submits.empty());
}

TEST_CASE("Threshold 0 replaces even an identical quote", "[mm][requote]") {
    mm::QuoteSet desired(1);
    desired[0].bid = quote(99.0, 5);
    mm::LiveQuoteSet live(1);
    live[0].bid = live_quote(7, 99.0, 5);

    const mm::RequotePlan plan = mm::plan_requote(desired, live, half_tick(), 0);

    CHECK(plan.cancels.size() == 1);
    CHECK(plan.submits.size() == 1);
}

TEST_CASE("A partially filled live quote is restocked at the same price", "[mm][requote]") {
    mm::QuoteSet desired(1);
    desired[0].ask = quote(101.0, 10);
    mm::LiveQuoteSet live(1);
    live[0].ask = live_quote(9, 101.0, 4); // 6 of 10 already sold

    const mm::RequotePlan plan = mm::plan_requote(desired, live, half_tick(), 5);

    REQUIRE(plan.cancels.size() == 1);
    CHECK(plan.cancels[0].id == mm::OrderId{9});
    REQUIRE(plan.submits.size() == 1);
    CHECK(plan.submits[0].side == mm::Side::Sell);
    CHECK(plan.submits[0].quote == quote(101.0, 10));
}

TEST_CASE("The two sides of an asset are decided independently", "[mm][requote]") {
    mm::QuoteSet desired(1);
    desired[0].bid = quote(100.0, 5); // moved 2 ticks: replace
    desired[0].ask = quote(101.0, 5); // unchanged: keep
    mm::LiveQuoteSet live(1);
    live[0].bid = live_quote(1, 99.0, 5);
    live[0].ask = live_quote(2, 101.0, 5);

    const mm::RequotePlan plan = mm::plan_requote(desired, live, half_tick(), 2);

    REQUIRE(plan.cancels.size() == 1);
    CHECK(plan.cancels[0].id == mm::OrderId{1});
    REQUIRE(plan.submits.size() == 1);
    CHECK(plan.submits[0].side == mm::Side::Buy);
}

TEST_CASE("A side that appears while the other is live is submitted on its own", "[mm][requote]") {
    mm::QuoteSet desired(1);
    desired[0].bid = quote(99.0, 5);
    desired[0].ask = quote(101.0, 5);
    mm::LiveQuoteSet live(1);
    live[0].bid = live_quote(1, 99.0, 5); // the ask was suppressed and is back

    const mm::RequotePlan plan = mm::plan_requote(desired, live, half_tick(), 1);

    CHECK(plan.cancels.empty());
    REQUIRE(plan.submits.size() == 1);
    CHECK(plan.submits[0].side == mm::Side::Sell);
}

TEST_CASE("Every asset is planned, in ascending order with Buy before Sell", "[mm][requote]") {
    mm::QuoteSet desired(2);
    desired[0].bid = quote(99.0, 5);
    desired[0].ask = quote(101.0, 5);
    desired[1].bid = quote(49.0, 5);
    desired[1].ask = quote(51.0, 5);
    mm::LiveQuoteSet live(2);
    live[0].bid = live_quote(10, 98.0, 5);
    live[0].ask = live_quote(11, 102.0, 5);
    live[1].bid = live_quote(20, 48.0, 5);
    live[1].ask = live_quote(21, 52.0, 5);

    const mm::RequotePlan plan = mm::plan_requote(desired, live, {0.5, 0.5}, 1);

    REQUIRE(plan.cancels.size() == 4);
    CHECK(plan.cancels[0].id == mm::OrderId{10});
    CHECK(plan.cancels[1].id == mm::OrderId{11});
    CHECK(plan.cancels[2].id == mm::OrderId{20});
    CHECK(plan.cancels[3].id == mm::OrderId{21});

    REQUIRE(plan.submits.size() == 4);
    CHECK((plan.submits[0].asset == 0 && plan.submits[0].side == mm::Side::Buy));
    CHECK((plan.submits[1].asset == 0 && plan.submits[1].side == mm::Side::Sell));
    CHECK((plan.submits[2].asset == 1 && plan.submits[2].side == mm::Side::Buy));
    CHECK((plan.submits[3].asset == 1 && plan.submits[3].side == mm::Side::Sell));
}

TEST_CASE("The threshold is measured in each asset's own ticks", "[mm][requote]") {
    mm::QuoteSet desired(2);
    desired[0].bid = quote(100.5, 5); // 0.5 above 100.0
    desired[1].bid = quote(100.5, 5); // 0.5 above 100.0
    mm::LiveQuoteSet live(2);
    live[0].bid = live_quote(1, 100.0, 5);
    live[1].bid = live_quote(2, 100.0, 5);

    // The same 0.5 is a single tick on asset 0 (tick 0.5) and 50 on asset 1 (tick 0.01).
    const mm::RequotePlan plan = mm::plan_requote(desired, live, {0.5, 0.01}, 2);

    REQUIRE(plan.cancels.size() == 1);
    CHECK(plan.cancels[0].id == mm::OrderId{2});
    REQUIRE(plan.submits.size() == 1);
    CHECK(plan.submits[0].asset == 1);
}

TEST_CASE("plan_requote rejects a negative threshold", "[mm][requote]") {
    CHECK_THROWS_AS(mm::plan_requote(mm::QuoteSet(1), mm::LiveQuoteSet(1), half_tick(), -1),
                    std::invalid_argument);
}

TEST_CASE("plan_requote rejects inputs that do not have one entry per asset", "[mm][requote]") {
    CHECK_THROWS_AS(mm::plan_requote(mm::QuoteSet(2), mm::LiveQuoteSet(1), {0.5, 0.5}, 1),
                    std::invalid_argument);
    CHECK_THROWS_AS(mm::plan_requote(mm::QuoteSet(2), mm::LiveQuoteSet(2), {0.5}, 1),
                    std::invalid_argument);
    CHECK_THROWS_AS(mm::plan_requote(mm::QuoteSet(1), mm::LiveQuoteSet(2), {0.5, 0.5}, 1),
                    std::invalid_argument);
}

TEST_CASE("plan_requote rejects an invalid tick size even with nothing live", "[mm][requote]") {
    for (const double bad : {0.0, -0.5, quiet_nan, infinity}) {
        CHECK_THROWS_AS(mm::plan_requote(mm::QuoteSet(1), mm::LiveQuoteSet(1), {bad}, 1),
                        std::invalid_argument);
    }
}

TEST_CASE("A rejected plan_requote does not touch its inputs", "[mm][requote]") {
    mm::QuoteSet desired(1);
    desired[0].bid = quote(99.0, 5);
    const mm::QuoteSet before = desired;

    CHECK_THROWS_AS(mm::plan_requote(desired, mm::LiveQuoteSet(1), {0.0}, 1),
                    std::invalid_argument);

    CHECK(desired == before);
}
