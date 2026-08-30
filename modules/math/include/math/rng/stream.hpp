#ifndef MMS_MATH_RNG_STREAM_HPP
#define MMS_MATH_RNG_STREAM_HPP

/// \file
/// Stream identity and RngHub: the construction-time factory that hands each
/// simulation subsystem a private, reproducible RngEngine derived from one
/// master Seed.

#include <cstdint>

#include <math/rng/engine.hpp>
#include <math/rng/seed.hpp>

namespace mms::math::rng {

enum class Subsystem : std::uint8_t { Price, AdverseFlow, Latency, PriceFactor };

enum class AssetKey : std::uint64_t {};

/// Sentinel for streams that are not per-asset (e.g. a global factor stream).
inline constexpr AssetKey no_asset{~std::uint64_t{0}};

struct StreamKey {
    Subsystem subsystem{};
    AssetKey asset = no_asset;

    friend constexpr bool operator==(const StreamKey&, const StreamKey&) = default;
};

class RngHub {
public:
    constexpr explicit RngHub(Seed master) noexcept : master_(master) {}

    /// A private engine for `key`. Pure function of (master seed, key): two hubs
    /// with the same master return identical engines for the same key.
    [[nodiscard]] constexpr RngEngine engine_for(StreamKey key) const noexcept {
        SplitMix64 sm{to_bits(master_)};
        sm.state ^= static_cast<std::uint64_t>(key.subsystem) * 0x9E37'79B9'7F4A'7C15ULL;
        sm.state ^= static_cast<std::uint64_t>(key.asset);

        const RngEngine::key_type k{sm.next(), sm.next()};
        // Low counter words = draw index (start 0); high words tag the stream,
        //  so distinct streams walk disjoint regions of the counter space.
        const RngEngine::counter_type c{0, 0, sm.next(), sm.next()};
        return RngEngine{k, c};
    }

    /// A sub-hub for parallel run #index
    [[nodiscard]] constexpr RngHub child(std::uint64_t index) const noexcept {
        return RngHub(spawn(master_, index));
    }

private:
    Seed master_;
};

} // namespace mms::math::rng

#endif // MMS_MATH_RNG_STREAM_HPP