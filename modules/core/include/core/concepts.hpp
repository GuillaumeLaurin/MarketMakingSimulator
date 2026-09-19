#ifndef MMS_CORE_CONCEPTS_HPP
#define MMS_CORE_CONCEPTS_HPP

/// \file
/// Constraints on the caller-supplied Price/Quantity-like types that
/// OrderBook, MatchingEngine, LatencyQueue etc. are parameterized on. core
/// never hardcodes what a "price" or "quantity" physically represents -
/// only what operations it needs to perform on them (compare, add,
/// subtract for partial-fill accounting).

#include <concepts>

namespace mms::core {

template<class T>
concept OrderedArithmetic =
    std::totally_ordered<T> && std::default_initializable<T> && requires(T a, T b) {
        { a + b } -> std::convertible_to<T>;
        { a - b } -> std::convertible_to<T>;
    };

} // namespace mms::core

#endif // MMS_CORE_CONCEPTS_HPP