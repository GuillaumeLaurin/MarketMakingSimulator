#ifndef MMS_MATH_RNG_SEED_HPP
#define MMS_MATH_RNG_SEED_HPP

/// \file
/// Seed: a strong 64-bit token for the master RNG seed. SplitMix64 is the
/// mixing primitive used to derive sub-seeds and stream keys.

#include <cstdint>

namespace mms::math::rng {

enum class Seed : std::uint64_t {};

[[nodiscard]] constexpr std::uint64_t to_bits(Seed s) noexcept {
    return static_cast<std::uint64_t>(s);
}

/// Stateful SplitMix64 (Steele et al.). Seed it, then pull as many 64-bit
/// words as needed; the sequence is a deterministic function of the seed.
struct SplitMix64 {
    std::uint64_t state = 0;

    [[nodiscard]] constexpr std::uint64_t next() noexcept {
        state += 0x9E37'79B9'7F4A'7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58'476D'1CE4'E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D0'49BB'1331'11EBULL;
        return z ^ (z >> 31);
    }
};

/// Child seed for parallel run #index (sensitivity sweeps).
[[nodiscard]] constexpr Seed spawn(Seed base, std::uint64_t index) noexcept {
    SplitMix64 sm{to_bits(base) ^ (index * 0x9E37'79B9'7F4A'7C15ULL)};
    return static_cast<Seed>(sm.next());
}

} // namespace mms::math::rng

#endif // MMS_MATH_RNG_SEED_HPP