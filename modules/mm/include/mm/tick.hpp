#ifndef MMS_MM_TICK_HPP
#define MMS_MM_TICK_HPP

/// \file
/// Tick-grid arithmetic. A quote price must sit on the asset's tick grid: a
/// bid is rounded DOWN and an ask rounded UP, so rounding only ever widens
/// the spread and never crosses it. Prices are doubles, and a double cannot
/// be trusted to land exactly on a grid line (100.3 / 0.1 is
/// 1002.9999999999999, and 1003 * 0.1 is 100.30000000000001), so the work is
/// done on whole tick counts, which compare and subtract exactly. That is
/// also what the requote threshold ("moved by at least N ticks") needs.

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <mm/types.hpp>

namespace mms::mm {

/// A price as a whole number of ticks.
using Ticks = std::int64_t;

namespace detail {

/// Slack, in ticks, within which a price counts as sitting exactly on a grid
/// line: a few dozen ulps of the tick count. Enough to absorb the error of
/// computing price / tick, not enough to pull a genuinely off-grid price to
/// the next line.
inline constexpr double snap_ulps = 32.0;

/// Largest |price / tick| accepted, well inside the range of Ticks.
inline constexpr double max_tick_position = 4.0e18;

inline void validate_tick(Price tick) {
    if (!std::isfinite(tick) || tick <= 0.0) {
        throw std::invalid_argument("tick: tick size must be finite and > 0");
    }
}

/// price / tick, validated, snapped onto the nearest whole number when it is
/// within snap_ulps of it.
inline double tick_position(Price price, Price tick) {
    validate_tick(tick);
    if (!std::isfinite(price)) {
        throw std::invalid_argument("tick: price must be finite");
    }
    const double position = price / tick;
    if (!(std::fabs(position) < max_tick_position)) {
        throw std::invalid_argument("tick: price is too many ticks away from zero");
    }
    const double nearest = std::round(position);
    const double slack =
        snap_ulps * std::numeric_limits<double>::epsilon() * std::fmax(1.0, std::fabs(position));
    return std::fabs(position - nearest) <= slack ? nearest : position;
}

} // namespace detail

/// The tick a bid at `price` rounds down to.
[[nodiscard]] inline Ticks bid_ticks(Price price, Price tick) {
    return static_cast<Ticks>(std::floor(detail::tick_position(price, tick)));
}

/// The tick an ask at `price` rounds up to.
[[nodiscard]] inline Ticks ask_ticks(Price price, Price tick) {
    return static_cast<Ticks>(std::ceil(detail::tick_position(price, tick)));
}

/// The tick closest to `price`. For turning a live quote's price, which was
/// produced by price_at, back into ticks.
[[nodiscard]] inline Ticks nearest_ticks(Price price, Price tick) {
    return static_cast<Ticks>(std::llround(detail::tick_position(price, tick)));
}

/// The price of a whole number of ticks.
[[nodiscard]] inline Price price_at(Ticks ticks, Price tick) {
    detail::validate_tick(tick);
    return static_cast<Price>(ticks) * tick;
}

struct BidAskTicks {
    Ticks bid;
    Ticks ask;
};

/// Rounds a raw bid down and a raw ask up, guaranteeing ask >= bid + 1.
/// Rounding outward already separates them by at least one tick whenever the
/// raw bid is strictly below the raw ask; the only case left to repair is a
/// raw spread so thin that both prices snap onto the same grid line, where
/// the ask is moved up one tick. A raw bid above the raw ask is a caller bug
/// (a strategy never produces it) and throws.
[[nodiscard]] inline BidAskTicks quote_ticks(Price bid_price, Price ask_price, Price tick) {
    if (bid_price > ask_price) {
        throw std::invalid_argument("tick: bid price must not exceed ask price");
    }
    BidAskTicks ticks{
        .bid = bid_ticks(bid_price, tick),
        .ask = ask_ticks(ask_price, tick)
    };
    if (ticks.ask <= ticks.bid) {
        ticks.ask = ticks.bid + 1;
    }
    return ticks;
}

} // namespace mms::mm

#endif // MMS_MM_TICK_HPP