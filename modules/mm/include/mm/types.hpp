#ifndef MMS_MM_TYPES_HPP
#define MMS_MM_TYPES_HPP

/// \file
/// The concrete vocabulary of the market-making layer. core is generic over
/// its Price and Quantity types; this header is where the project picks
/// them (Price = double, Quantity = int64_t) and gives every core template
/// its instantiated name, so the rest of mms never spells out
/// core::OrderBook<double, std::int64_t> by hand.

#include <cstddef>
#include <cstdint>

#include <core/concepts.hpp>
#include <core/matching_engine.hpp>
#include <core/order.hpp>
#include <core/order_book.hpp>
#include <core/order_id.hpp>
#include <core/side.hpp>
#include <core/trade.hpp>

namespace mms::mm {

/// Dollars per share. A double, like everything in math, so prices reach
/// the Eigen vectors without any conversion.
using Price = double;

/// A whole number of shares. Signed on purpose: a position is negative when
/// short, while the quantity of an order is always positive. An integer
/// keeps fill accounting exact, with no floating-point dust left over.
using Quantity = std::int64_t;

/// Position of an asset in the simulation's asset list. Positional index
/// only (it is also the Eigen index), NOT a stable identity: that is
/// math::rng::AssetKey's job, which is derived from the symbol.
using AssetId = std::size_t;

static_assert(core::OrderedArithmetic<Price>);
static_assert(core::OrderedArithmetic<Quantity>);

using core::MarketTimeInForce;
using core::OrderId;
using core::Side;
using core::TimeInForce;

using LimitOrder = core::LimitOrder<Price, Quantity>;
using MarketOrder = core::MarketOrder<Quantity>;
using Order = core::Order<Price, Quantity>;
using Trade = core::Trade<Price, Quantity>;
using OrderBook = core::OrderBook<Price, Quantity>;
using MatchingEngine = core::MatchingEngine<Price, Quantity>;
using MatchingEngineObserver = core::MatchingEngineObserver<Price, Quantity>;

/// The one place a Quantity becomes a double (MarketView construction, for
/// the inventory vector handed to math). Exact up to 2^53 shares, far above
/// any position this simulator will hold.
[[nodiscard]] constexpr double to_double(Quantity quantity) noexcept {
    return static_cast<double>(quantity);
}

} // namespace mms::mm

#endif // MMS_MM_TYPES_HPP