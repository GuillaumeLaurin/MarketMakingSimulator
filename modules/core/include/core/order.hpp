#ifndef MMS_CORE_ORDER_HPP
#define MMS_CORE_ORDER_HPP

/// \file
/// LimitOrder / MarketOrder / Order: the two order kinds, kept as distinct
/// types rather than one struct with a type-tag enum, so the one invalid
/// combination (a Market order resting indefinitely, i.e. GTC) is
/// unrepresentable at the type level - MarketOrder's TimeInForce is a
/// smaller enum that simply has no GTC value, rather than a shared enum
/// validated at runtime. Order is a closed sum type (std::variant), not a
/// virtual class hierarchy - consistent with this project's stance against
/// deep inheritance.

#include <variant>

#include <core/concepts.hpp>
#include <core/side.hpp>

namespace mms::core {

/// LimitOrder: GTC rests in the book until filled or cancelled; IOC fills
/// whatever it can immediately and cancels the remainder; FOK fills
/// completely and immediately or is cancelled entirely (no partial fill).
enum class TimeInForce : std::uint8_t { GTC, IOC, FOK };

/// MarketOrder has no price, so it can never rest - GTC is not a valid
/// value for it, hence this separate, smaller enum rather than reusing
/// TimeInForce with a runtime check.
enum class MarketTimeInForce : std::uint8_t { IOC, FOK };

template<OrderedArithmetic Price, OrderedArithmetic Quantity> struct LimitOrder {
    Side side;
    Price price;
    Quantity quantity;
    TimeInForce time_in_force;
};

template<OrderedArithmetic Quantity> struct MarketOrder {
    Side side;
    Quantity quantity;
    MarketTimeInForce time_in_force;
};

template<OrderedArithmetic Price, OrderedArithmetic Quantity>
using Order = std::variant<LimitOrder<Price, Quantity>, MarketOrder<Quantity>>;

} // namespace mms::core

#endif // MMS_CORE_ORDER_HPP