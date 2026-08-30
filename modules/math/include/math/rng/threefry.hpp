#ifndef MMS_MATH_RNG_THREEFRY_HPP
#define MMS_MATH_RNG_THREEFRY_HPP

/// \file
/// ThreefryBlock: the Threefry-4x64 keyed bijection (Salmon et al., 2011), a
/// model of the BlockFunction concept. ARX only (add / rotate / xor), no
/// multiply, so it needs no platform shim. Fallback for PhiloxBlock.

#include <array>
#include <cstddef>
#include <cstdint>

namespace mms::math::rng {

struct ThreefryBlock {
    using word_type = std::uint64_t;
    using result_type = word_type;

    static constexpr std::size_t word_count = 4;
    static constexpr std::size_t key_words = 4;
    static constexpr std::size_t counter_words = 4;

    using counter_type = std::array<word_type, counter_words>;
    using key_type = std::array<word_type, key_words>;

    // Skein key-schedule parity constant.
    static constexpr word_type key_parity = 0x1BD1'1BDA'A9FC'1A22ULL;

    // Rotation constants for Threefry-4x64, two per round, cycling every 8
    // rounds. From the Random123 reference.
    static constexpr std::array<unsigned, 8> rot_0 = {14, 52, 23, 5, 25, 46, 58, 32};
    static constexpr std::array<unsigned, 8> rot_1 = {16, 57, 40, 37, 33, 12, 22, 32};

    [[nodiscard]] static constexpr word_type rotl(word_type x, unsigned n) noexcept {
        return (n == 0) ? x : ((x << n) | (x >> (64U - n)));
    }

    /// Apply \p rounds Threefry rounds. Threefry-4x64-20 means rounds == 20.
    [[nodiscard]] static constexpr counter_type
    block(counter_type ctr, key_type key, unsigned rounds) noexcept {
        // Round-constant tables are indexed by a bounded loop counter: k is
        // r % 8 (rot_0 / rot_1 hold 8 entries) and the key-schedule index is
        // (s + i) % 5 (ks holds 5 entries). Both are provably in range, so the
        // runtime array subscripts below are safe.
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)

        // Extended key schedule: 5 words, last = parity xor (xor of the key).
        const std::array<word_type, 5> ks = {
            key[0],
            key[1],
            key[2],
            key[3],
            key_parity ^ key[0] ^ key[1] ^ key[2] ^ key[3],
        };

        // Subkey 0.
        word_type x0 = ctr[0] + ks[0];
        word_type x1 = ctr[1] + ks[1];
        word_type x2 = ctr[2] + ks[2];
        word_type x3 = ctr[3] + ks[3];

        for (unsigned r = 0; r < rounds; ++r) {
            const unsigned k = r % 8;

            // Even or odd rounds mix different word pairs; this is the 4x64
            // word permutation folded into the round.
            if ((r & 1U) == 0U) {
                x0 += x1;
                x1 = rotl(x1, rot_0[k]);
                x1 ^= x0;
                x2 += x3;
                x3 = rotl(x3, rot_1[k]);
                x3 ^= x2;
            } else {
                x0 += x3;
                x3 = rotl(x3, rot_0[k]);
                x3 ^= x0;
                x2 += x1;
                x1 = rotl(x1, rot_1[k]);
                x1 ^= x2;
            }

            // Inject a subkey after every fourth round.
            if ((r % 4U) == 3U) {
                const auto s = word_type{(r / 4U) + 1U};
                x0 += ks[(s + 0U) % 5U];
                x1 += ks[(s + 1U) % 5U];
                x2 += ks[(s + 2U) % 5U];
                x3 += ks[(s + 3U) % 5U] + s;
            }
        }

        return counter_type{x0, x1, x2, x3};
        // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    }
};

} // namespace mms::math::rng

#endif // MMS_MATH_RNG_THREEFRY_HPP
