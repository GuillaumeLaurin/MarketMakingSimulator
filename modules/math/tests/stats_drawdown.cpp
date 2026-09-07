/// \file
/// Tests for DrawdownTracker. Hand-computed equity series pin the running peak,
/// absolute and relative drawdown, and the underwater-duration counters.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <math/stats/drawdown.hpp>

namespace {

using mms::math::stats::DrawdownTracker;

constexpr double inf = std::numeric_limits<double>::infinity();
const double nan_value = std::numeric_limits<double>::quiet_NaN();

} // namespace

TEST_CASE("DrawdownTracker is empty before any sample", "[stats][drawdown]") {
    const DrawdownTracker d;
    CHECK(d.count() == 0);
    CHECK(std::isnan(d.peak()));
    CHECK(std::isnan(d.current_drawdown()));
    CHECK(std::isnan(d.max_drawdown()));
    CHECK_FALSE(d.current_relative_drawdown().has_value());
    CHECK_FALSE(d.max_relative_drawdown().has_value());
    CHECK(d.current_underwater() == 0);
    CHECK(d.longest_underwater() == 0);
}

TEST_CASE("DrawdownTracker with one positive sample has zero drawdown", "[stats][drawdown]") {
    DrawdownTracker d;
    d.add(100.0);
    CHECK(d.count() == 1);
    CHECK(d.peak() == 100.0);
    CHECK(d.current_drawdown() == 0.0);
    CHECK(d.max_drawdown() == 0.0);
    REQUIRE(d.current_relative_drawdown().has_value());
    CHECK(*d.current_relative_drawdown() == 0.0);
    REQUIRE(d.max_relative_drawdown().has_value());
    CHECK(*d.max_relative_drawdown() == 0.0);
    CHECK(d.current_underwater() == 0);
    CHECK(d.longest_underwater() == 0);
}

TEST_CASE("DrawdownTracker sees no drawdown on a monotonically rising series",
          "[stats][drawdown]") {
    DrawdownTracker d;
    for (const double e : {100.0, 110.0, 120.0, 130.0}) {
        d.add(e);
    }
    CHECK(d.peak() == 130.0);
    CHECK(d.max_drawdown() == 0.0);
    CHECK(d.current_drawdown() == 0.0);
    CHECK(d.current_underwater() == 0);
    CHECK(d.longest_underwater() == 0);
    REQUIRE(d.max_relative_drawdown().has_value());
    CHECK(*d.max_relative_drawdown() == 0.0);
}

TEST_CASE("DrawdownTracker on a hand-computed series", "[stats][drawdown]") {
    // equity: 100 120 90 95 130 110 105 140
    //   peak:            120 -> 130 -> 140
    //   worst abs dd = 120 - 90 = 30 ; worst rel dd = 30 / 120 = 0.25
    //   underwater runs: {90,95} len 2, then {110,105} len 2 -> longest 2
    const std::array<double, 8> equity{100.0, 120.0, 90.0, 95.0, 130.0, 110.0, 105.0, 140.0};
    DrawdownTracker d;

    for (std::size_t i = 0; i < 4; ++i) {
        d.add(equity[i]);
    }
    // snapshot after 100 120 90 95
    CHECK(d.peak() == 120.0);
    CHECK(d.current_drawdown() == Catch::Approx(25.0));
    CHECK(d.max_drawdown() == Catch::Approx(30.0));
    CHECK(d.current_underwater() == 2);
    CHECK(d.longest_underwater() == 2);
    REQUIRE(d.max_relative_drawdown().has_value());
    CHECK(*d.max_relative_drawdown() == Catch::Approx(0.25));

    for (std::size_t i = 4; i < equity.size(); ++i) {
        d.add(equity[i]);
    }
    // final: last add is a new peak (140)
    CHECK(d.count() == 8);
    CHECK(d.peak() == 140.0);
    CHECK(d.current_drawdown() == 0.0);
    CHECK(d.max_drawdown() == Catch::Approx(30.0));
    CHECK(d.current_underwater() == 0);
    CHECK(d.longest_underwater() == 2);
    REQUIRE(d.current_relative_drawdown().has_value());
    CHECK(*d.current_relative_drawdown() == 0.0);
    REQUIRE(d.max_relative_drawdown().has_value());
    CHECK(*d.max_relative_drawdown() == Catch::Approx(0.25));
}

TEST_CASE("DrawdownTracker resets the underwater counter on recovery to the peak",
          "[stats][drawdown]") {
    DrawdownTracker d;
    for (const double e : {100.0, 90.0, 80.0, 100.0, 90.0}) {
        d.add(e);
    }
    // {90,80} len 2, recovery to 100 resets, then {90} len 1
    CHECK(d.current_underwater() == 1);
    CHECK(d.longest_underwater() == 2);
    CHECK(d.peak() == 100.0);
    CHECK(d.max_drawdown() == Catch::Approx(20.0)); // 100 - 80
    CHECK(d.current_drawdown() == Catch::Approx(10.0));
}

TEST_CASE("DrawdownTracker relative drawdown is absent while the peak is not positive",
          "[stats][drawdown]") {
    DrawdownTracker d;
    for (const double e : {-50.0, -80.0, -40.0}) {
        d.add(e);
    }
    CHECK_FALSE(d.current_relative_drawdown().has_value());
    CHECK_FALSE(d.max_relative_drawdown().has_value());
    // absolute drawdown still works: peak -50, trough -80
    CHECK(d.max_drawdown() == Catch::Approx(30.0));
    CHECK(d.peak() == -40.0);

    // cross into positive territory
    d.add(20.0); // new peak, now positive
    d.add(10.0); // 10 < 20 -> rel dd 0.5
    REQUIRE(d.current_relative_drawdown().has_value());
    CHECK(*d.current_relative_drawdown() == Catch::Approx(0.5));
    REQUIRE(d.max_relative_drawdown().has_value());
    CHECK(*d.max_relative_drawdown() == Catch::Approx(0.5));
    // the absolute max still spans the negative era
    CHECK(d.max_drawdown() == Catch::Approx(30.0));
}

TEST_CASE("DrawdownTracker ignores non-finite equity", "[stats][drawdown]") {
    DrawdownTracker d;
    d.add(100.0);
    d.add(80.0);
    const std::int64_t n0 = d.count();
    const double peak0 = d.peak();
    const double maxdd0 = d.max_drawdown();
    const std::int64_t uw0 = d.current_underwater();

    d.add(nan_value);
    d.add(inf);
    d.add(-inf);

    CHECK(d.count() == n0);
    CHECK(d.peak() == peak0);
    CHECK(d.max_drawdown() == maxdd0);
    CHECK(d.current_underwater() == uw0);
}
