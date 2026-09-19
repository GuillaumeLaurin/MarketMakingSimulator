#ifndef MMS_CORE_TRADE_HPP
#define MMS_CORE_TRADE_HPP

/// \file
/// Trade: the result of a match between an aggressing order and one or more
/// resting orders.

#include <core/concepts.hpp>
#include <core/order_id.hpp>

namespace mms::core {

template<OrderedArithmetic Price, OrderedArithmetic Quantity> struct Trade {
    OrderId buy_order_id;
    OrderId sell_order_id;
    Price price;
    Quantity quantity;
};

} // namespace mms::core

#endif // MMS_CORE_TRADE_HPP