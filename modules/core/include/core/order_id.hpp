#ifndef MMS_CORE_ORDER_ID_HPP
#define MMS_CORE_ORDER_ID_HPP

/// \file
/// OrderId: an opaque identifier assigned by core itself when an order is
/// submitted. Not a template parameter - unlike Price/Quantity, this is an
/// internal core concern, not something the caller customizes.

#include <cstdint>

namespace mms::core {

enum class OrderId : std::uint64_t {};
    
} // namespace mms::core

#endif // MMS_CORE_ORDER_ID_HPP