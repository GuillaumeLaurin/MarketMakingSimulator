#pragma once
#ifndef MMS_MATH_RNG_PHILOX_HPP
#define MMS_MATH_RNG_PHILOX_HPP

/// \file 
/// PhiloxBlock: the Philox-4x64 keyed bijection (Salmon et al., 2011), a model
/// of the BlockFunction concept. Stateless policy for CounterBasedEngine.

#include <array>
#include <cstddef>
#include <cstdint>

#include <math/bits.hpp>

namespace mms::math::rng {

struct PhiloxBlock {
    using word_type = std::uint64_t;

    static constexpr std::size_t word_count    = 4;
    static constexpr std::size_t key_words     = 2;
    static constexpr std::size_t counter_words = 4;

    using counter_type = std::array<word_type, counter_words>;
    using key_type = std::array<word_type, key_words>;

    /// Round multipliers and the Weyl key-schedule increments (golden ratio,
    /// sqrt(3) - 1). Values from the Random123 reference.
    static constexpr word_type multiplier_0 = 0xD2E7'470E'E14C'6C93ULL;
    static constexpr word_type multiplier_1 = 0xCA5A'8263'9512'1157ULL;
    static constexpr word_type weyl_0       = 0x9E37'79B9'7F4A'7C15ULL;
    static constexpr word_type weyl_1       = 0xBB67'AE85'84CA'A73BULL;

    /// Apply \p rounds Philox rounds. Rounds 0 uses \p key unchanged; every
    /// later round bumps the key by the Weyl constants first. Philox-4x64-10
    /// means rounds == 10
    [[nodiscard]] static constexpr counter_type
    block(counter_type ctr, key_type key, unsigned rounds) noexcept
    {
        for (unsigned r = 0; r < rounds; ++r)
        {
            if (r != 0) {
                key[0] += weyl_0;
                key[1] += weyl_1;
            }
            const word_type hi0 = mulhi64(multiplier_0, ctr[0]);
            const word_type lo0 = multiplier_0 * ctr[0];
            const word_type hi1 = mulhi64(multiplier_1, ctr[2]);
            const word_type lo1 = multiplier_1 * ctr[2];

            ctr = counter_type{
                hi1 ^ ctr[1] ^ key[0],
                lo1,
                hi0 ^ ctr[3] ^ key[1],
                lo0
            };
        }
        
        return ctr;
    }
};

}

#endif // MMS_MATH_RNG_PHILOX_HPP