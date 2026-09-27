/// \file
/// Tests for mm/risk.hpp: the two predicates on their own (the hard position
/// limit, where reducing exposure is always allowed, and the first-order
/// portfolio cap rule), then apply_limits, which removes disallowed quotes
/// in place and must fail closed (throw) on inputs that would otherwise
/// silently disable the protection.

#include <cstddef>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mm/quote.hpp>
#include <mm/risk.hpp>
#include <mm/types.hpp>

#include <math/types.hpp>

namespace mm = mms::mm;
namespace math = mms::math;

namespace {

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

math::Vector vec(std::initializer_list<double> values) {
    math::Vector result(static_cast<Eigen::Index>(values.size()));
    Eigen::Index index = 0;
    for (const double value : values) {
        result(index++) = value;
    }
    return result;
}

mm::Quote quote(mm::Price price, mm::Quantity quantity) {
    return mm::Quote{.price = price, .quantity = quantity};
}

/// One asset per size, each quoting a bid and an ask of that size.
mm::QuoteSet quotes_of_sizes(std::initializer_list<mm::Quantity> sizes) {
    mm::QuoteSet quotes;
    for (const mm::Quantity size : sizes) {
        quotes.push_back(mm::AssetQuotes{.bid = quote(99.0, size), .ask = quote(101.0, size)});
    }
    return quotes;
}

mm::RiskLimits limits_of(std::vector<mm::Quantity> max_abs,
                         std::optional<double> cap = std::nullopt) {
    return mm::RiskLimits{.max_abs_position = std::move(max_abs), .max_portfolio_risk = cap};
}

} // namespace

TEST_CASE("A buy is allowed up to exactly the limit and refused past it", "[mm][risk]") {
    CHECK(mm::position_limit_allows(0.0, mm::Side::Buy, 5, 10));
    CHECK(mm::position_limit_allows(5.0, mm::Side::Buy, 5, 10)); // lands exactly on 10
    CHECK_FALSE(mm::position_limit_allows(5.0, mm::Side::Buy, 6, 10));
}

TEST_CASE("A sell is allowed down to exactly minus the limit and refused past it", "[mm][risk]") {
    CHECK(mm::position_limit_allows(0.0, mm::Side::Sell, 5, 10));
    CHECK(mm::position_limit_allows(-5.0, mm::Side::Sell, 5, 10)); // lands exactly on -10
    CHECK_FALSE(mm::position_limit_allows(-5.0, mm::Side::Sell, 6, 10));
}

TEST_CASE("At the limit only the reducing side is allowed", "[mm][risk]") {
    CHECK_FALSE(mm::position_limit_allows(10.0, mm::Side::Buy, 1, 10));
    CHECK(mm::position_limit_allows(10.0, mm::Side::Sell, 1, 10));

    CHECK_FALSE(mm::position_limit_allows(-10.0, mm::Side::Sell, 1, 10));
    CHECK(mm::position_limit_allows(-10.0, mm::Side::Buy, 1, 10));
}

TEST_CASE("Past the limit, a side that shrinks the position is still allowed", "[mm][risk]") {
    // Long 15 against a limit of 10: selling 3 leaves 12, still over, but closer.
    CHECK(mm::position_limit_allows(15.0, mm::Side::Sell, 3, 10));
    CHECK_FALSE(mm::position_limit_allows(15.0, mm::Side::Buy, 1, 10));

    CHECK(mm::position_limit_allows(-15.0, mm::Side::Buy, 3, 10));
    CHECK_FALSE(mm::position_limit_allows(-15.0, mm::Side::Sell, 1, 10));
}

TEST_CASE("A quote that flips the position past the opposite limit is refused", "[mm][risk]") {
    // Long 8: selling 18 ends at -10 (allowed), 20 ends at -12 (refused).
    CHECK(mm::position_limit_allows(8.0, mm::Side::Sell, 12, 10));
    CHECK(mm::position_limit_allows(8.0, mm::Side::Sell, 18, 10));
    CHECK_FALSE(mm::position_limit_allows(8.0, mm::Side::Sell, 20, 10));

    // Beyond the limit already, a flip that ends farther out is not a reduction.
    CHECK_FALSE(mm::position_limit_allows(15.0, mm::Side::Sell, 40, 10));
}

TEST_CASE("A flat asset may not quote more than the limit on either side", "[mm][risk]") {
    CHECK_FALSE(mm::position_limit_allows(0.0, mm::Side::Buy, 11, 10));
    CHECK_FALSE(mm::position_limit_allows(0.0, mm::Side::Sell, 11, 10));
    CHECK(mm::position_limit_allows(0.0, mm::Side::Buy, 10, 10));
}

TEST_CASE("The portfolio cap allows a side unless it raises the risk", "[mm][risk]") {
    CHECK_FALSE(mm::portfolio_cap_allows(mm::Side::Buy, 2.0));
    CHECK(mm::portfolio_cap_allows(mm::Side::Sell, 2.0));

    CHECK(mm::portfolio_cap_allows(mm::Side::Buy, -2.0));
    CHECK_FALSE(mm::portfolio_cap_allows(mm::Side::Sell, -2.0));

    // Zero gradient: neither side changes the risk to first order.
    CHECK(mm::portfolio_cap_allows(mm::Side::Buy, 0.0));
    CHECK(mm::portfolio_cap_allows(mm::Side::Sell, 0.0));
}

TEST_CASE("The predicates are noexcept", "[mm][risk]") {
    STATIC_REQUIRE(noexcept(mm::position_limit_allows(0.0, mm::Side::Buy, 1, 1)));
    STATIC_REQUIRE(noexcept(mm::portfolio_cap_allows(mm::Side::Buy, 0.0)));
}

TEST_CASE("apply_limits leaves every quote alone when nothing is breached", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5, 5});
    const mm::QuoteSet before = quotes;

    mm::apply_limits(quotes, limits_of({10, 10}), vec({0.0, 3.0}), vec({0.0, 0.0}));

    CHECK(quotes == before);
}

TEST_CASE("apply_limits drops the bid near the long limit and keeps the ask", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5});

    // Long 8: a full bid fill would reach 13 > 10.
    mm::apply_limits(quotes, limits_of({10}), vec({8.0}), vec({0.0}));

    CHECK_FALSE(quotes[0].bid.has_value());
    REQUIRE(quotes[0].ask.has_value());
    CHECK(*quotes[0].ask == quote(101.0, 5)); // the survivor is untouched
}

TEST_CASE("apply_limits drops the ask near the short limit and keeps the bid", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5});

    mm::apply_limits(quotes, limits_of({10}), vec({-8.0}), vec({0.0}));

    CHECK_FALSE(quotes[0].ask.has_value());
    REQUIRE(quotes[0].bid.has_value());
    CHECK(*quotes[0].bid == quote(99.0, 5));
}

TEST_CASE("apply_limits keeps a quote whose full fill lands exactly on the limit", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5});

    mm::apply_limits(quotes, limits_of({10}), vec({5.0}), vec({0.0}));

    CHECK(quotes[0].bid.has_value()); // 5 + 5 == 10
    CHECK(quotes[0].ask.has_value());
}

TEST_CASE("apply_limits removes a quote that would overshoot rather than shrinking it",
          "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5});

    // Long 7 with room for only 3 more: the bid of 5 goes, it is not clipped to 3.
    mm::apply_limits(quotes, limits_of({10}), vec({7.0}), vec({0.0}));

    CHECK_FALSE(quotes[0].bid.has_value());
}

TEST_CASE("apply_limits lets an asset past its limit quote the side that reduces it",
          "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({3});

    mm::apply_limits(quotes, limits_of({10}), vec({15.0}), vec({0.0}));

    CHECK_FALSE(quotes[0].bid.has_value());
    CHECK(quotes[0].ask.has_value()); // 15 - 3 = 12, still over but smaller
}

TEST_CASE("apply_limits applies each asset's own limit", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5, 5});

    mm::apply_limits(quotes, limits_of({10, 100}), vec({8.0, 8.0}), vec({0.0, 0.0}));

    CHECK_FALSE(quotes[0].bid.has_value()); // 8 + 5 > 10
    CHECK(quotes[1].bid.has_value());       // 8 + 5 <= 100
}

TEST_CASE("apply_limits ignores a side that is not being quoted", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5});
    quotes[0].ask.reset();

    mm::apply_limits(quotes, limits_of({10}), vec({-8.0}), vec({0.0}));

    CHECK_FALSE(quotes[0].ask.has_value()); // still nothing, not resurrected
    CHECK(quotes[0].bid.has_value());
}

TEST_CASE("The portfolio cap does nothing while it is disabled", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5, 5});
    const mm::QuoteSet before = quotes;

    // A huge risk, but no cap set.
    mm::apply_limits(quotes, limits_of({100, 100}), vec({2.0, -1.0}), vec({1000.0, -1000.0}));

    CHECK(quotes == before);
}

TEST_CASE("The portfolio cap does nothing at or below the cap", "[mm][risk]") {
    mm::QuoteSet below = quotes_of_sizes({5, 5});
    const mm::QuoteSet before = below;

    // q . sigma_q = 2 * 3 + (-1) * (-2) = 8.
    mm::apply_limits(below, limits_of({100, 100}, 9.0), vec({2.0, -1.0}), vec({3.0, -2.0}));
    CHECK(below == before);

    mm::QuoteSet exactly = quotes_of_sizes({5, 5});
    mm::apply_limits(exactly, limits_of({100, 100}, 8.0), vec({2.0, -1.0}), vec({3.0, -2.0}));
    CHECK(exactly == before); // the cap is exceeded only strictly above it
}

TEST_CASE("Above the cap, each asset loses the side that raises the portfolio risk", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5, 5});

    // q . sigma_q = 8 > 5. Asset 0 has (Sigma q) > 0: buying adds risk.
    // Asset 1 has (Sigma q) < 0: selling adds risk.
    mm::apply_limits(quotes, limits_of({100, 100}, 5.0), vec({2.0, -1.0}), vec({3.0, -2.0}));

    CHECK_FALSE(quotes[0].bid.has_value());
    CHECK(quotes[0].ask.has_value());
    CHECK(quotes[1].bid.has_value());
    CHECK_FALSE(quotes[1].ask.has_value());
}

TEST_CASE("Above the cap, an asset with a zero gradient keeps both sides", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5, 5});

    // q . sigma_q = 2 * 3 = 6 > 5, but asset 1's entry of Sigma q is 0.
    mm::apply_limits(quotes, limits_of({100, 100}, 5.0), vec({2.0, 0.0}), vec({3.0, 0.0}));

    CHECK(quotes[1].bid.has_value());
    CHECK(quotes[1].ask.has_value());
}

TEST_CASE("A side is removed when either the position limit or the cap refuses it", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5, 5});

    // q . sigma_q = 8 * (-1) + 4 * 5 = 12 > 5.
    // Asset 0: the position limit refuses the bid (8 + 5 > 10), the cap refuses the ask.
    // Asset 1: the cap refuses the bid, the ask survives both.
    mm::apply_limits(quotes, limits_of({10, 100}, 5.0), vec({8.0, 4.0}), vec({-1.0, 5.0}));

    CHECK_FALSE(quotes[0].bid.has_value());
    CHECK_FALSE(quotes[0].ask.has_value());
    CHECK_FALSE(quotes[1].bid.has_value());
    CHECK(quotes[1].ask.has_value());
}

TEST_CASE("apply_limits on zero assets is a no-op", "[mm][risk]") {
    mm::QuoteSet quotes;

    CHECK_NOTHROW(mm::apply_limits(quotes, limits_of({}), vec({}), vec({})));
    CHECK(quotes.empty());
}

TEST_CASE("apply_limits rejects inputs that do not have one entry per asset", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5, 5});

    CHECK_THROWS_AS(mm::apply_limits(quotes, limits_of({10}), vec({0.0, 0.0}), vec({0.0, 0.0})),
                    std::invalid_argument);
    CHECK_THROWS_AS(mm::apply_limits(quotes, limits_of({10, 10}), vec({0.0}), vec({0.0, 0.0})),
                    std::invalid_argument);
    CHECK_THROWS_AS(mm::apply_limits(quotes, limits_of({10, 10}), vec({0.0, 0.0}), vec({0.0})),
                    std::invalid_argument);
}

TEST_CASE("apply_limits rejects a non-positive position limit", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5, 5});

    CHECK_THROWS_AS(mm::apply_limits(quotes, limits_of({10, 0}), vec({0.0, 0.0}), vec({0.0, 0.0})),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        mm::apply_limits(quotes, limits_of({-10, 10}), vec({0.0, 0.0}), vec({0.0, 0.0})),
        std::invalid_argument);
}

TEST_CASE("apply_limits rejects an invalid portfolio cap", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5});

    for (const double bad : {0.0, -1.0, quiet_nan, infinity}) {
        CHECK_THROWS_AS(mm::apply_limits(quotes, limits_of({10}, bad), vec({0.0}), vec({0.0})),
                        std::invalid_argument);
    }
}

TEST_CASE("apply_limits fails closed on a non-finite inventory", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5});

    // A NaN would compare false against every limit and keep every quote.
    CHECK_THROWS_AS(mm::apply_limits(quotes, limits_of({10}), vec({quiet_nan}), vec({0.0})),
                    std::invalid_argument);
    CHECK_THROWS_AS(mm::apply_limits(quotes, limits_of({10}), vec({infinity}), vec({0.0})),
                    std::invalid_argument);
}

TEST_CASE("apply_limits fails closed on a non-finite Sigma q, but only when the cap uses it",
          "[mm][risk]") {
    mm::QuoteSet with_cap = quotes_of_sizes({5});
    CHECK_THROWS_AS(mm::apply_limits(with_cap, limits_of({10}, 5.0), vec({1.0}), vec({quiet_nan})),
                    std::invalid_argument);

    mm::QuoteSet without_cap = quotes_of_sizes({5});
    CHECK_NOTHROW(mm::apply_limits(without_cap, limits_of({10}), vec({1.0}), vec({quiet_nan})));
    CHECK(without_cap[0].bid.has_value());
}

TEST_CASE("A rejected apply_limits leaves the quotes untouched", "[mm][risk]") {
    mm::QuoteSet quotes = quotes_of_sizes({5});
    const mm::QuoteSet before = quotes;

    // The position would have removed the bid; the invalid cap must stop it first.
    CHECK_THROWS_AS(mm::apply_limits(quotes, limits_of({10}, -1.0), vec({8.0}), vec({0.0})),
                    std::invalid_argument);

    CHECK(quotes == before);
}
