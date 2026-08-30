#pragma once
#ifndef MMS_MATH_BITS_HPP
#define MMS_MATH_BITS_HPP

/// \file
/// Low-level integer ops for MMS::math : the high half of a 64x64 -> 128
/// unsigned multiply. Needed by PhiloxBlock and by Lemire's bounded integer
/// sampler. No dependency on any other MMS::math header.

#include <cstdint>
#include <type_traits>

#if defined(_MSC_VER) && defined(_M_X64)
#   include <intrin.h>
#endif 

namespace mms::math {

namespace detail {
#if defined(__SIZEOF_INT128__)
/// __extension__ silences -Wpedantic (-Werror on the dev presets) for
/// non-standard 128-bit type; uses of the alias do not re-trigger it.
__extension__ using uint128 = unsigned __int128;
#endif
} // namespace detail

/// High 64 bits of the 128-bit product 'a * b'. constexpr on every target: the
/// portable 32x32 decomposition is used during constant evaluation.
[[nodiscard]] constexpr std::uint64_t mulhi64(std::uint64_t a,
                                              std::uint64_t b) noexcept
{
#if defined(__SIZEOF_INT128__)
    return static_cast<std::uint64_t>(
        (static_cast<detail::uint128>(a) * b) >> 64);
#else
    if (!std::is_constant_evaluated()) {
#if defined(_MSC_VER) && defined(_M_X64)
        return __umulh(a, b);
#   endif
    }
    const std::uint64_t a_lo = a & 0xFFFF'FFFFULL;
    const std::uint64_t a_hi = a >> 32;
    const std::uint64_t b_lo = b & 0xFFFF'FFFFULL;
    const std::uint64_t b_hi = b >> 32;

    const std::uint64_t lo_lo = a_lo * b_lo;
    const std::uint64_t hi_lo = a_hi * b_lo;
    const std::uint64_t lo_hi = a_lo * b_hi;
    const std::uint64_t hi_hi = a_hi * b_hi;

    const std::uint64_t cross =
        (lo_lo >> 32) + (hi_lo & 0xFFFF'FFFFULL) + (lo_hi & 0xFFFF'FFFFULL);
    
    return hi_hi + (hi_lo >> 32) + (lo_hi >> 32) + (cross >> 32);
#endif
}

} // namespace mms::math

#endif // MMS_MATH_BITS_HPP