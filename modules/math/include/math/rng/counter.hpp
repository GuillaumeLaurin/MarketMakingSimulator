#ifndef MMS_MATH_RNG_COUNTER_HPP
#define MMS_MATH_RNG_COUNTER_HPP

/// \file
/// Counter<N>: an N-word little-endian unsigned integer, the position of a 
/// counter-based generator. Increment-with-carry lives here as a public,
/// independently testable operation

#include <array>
#include <cstddef>
#include <cstdint>

namespace mms::math::rng {

template <std::size_t N>
struct Counter {
    std::array<std::uint64_t, N> words{};

    constexpr void bump(std::uint64_t add = 1) noexcept {
        for (std::size_t i = 0; i < N && add != 0; ++i) {
            const std::uint64_t before = words[i];
            words[i] += add;
            add = (words[i] < before) ? 1U : 0U;
        }
    }

    [[nodiscard]] constexpr std::uint64_t operator[](std::size_t i) const noexcept {
        return words[i];
    }

    friend constexpr bool operator==(const Counter&, const Counter&) = default;
};

} // namespace mms::math::rng

#endif // MMS_MATH_RNG_COUNTER_HPP