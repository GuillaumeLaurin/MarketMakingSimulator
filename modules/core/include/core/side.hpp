#ifndef MMS_CORE_SIDE_HPP
#define MMS_CORE_SIDE_HPP

/// \file
/// Side: witch direction an order acts in a two-sided matching machine.

#include <cstdint>

namespace mms::core {

enum class Side : std::uint8_t { Buy, Sell };

} // namespace mms::core

#endif // MMS_CORE_SIDE_HPP