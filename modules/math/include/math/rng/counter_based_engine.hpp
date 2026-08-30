#ifndef MMS_MATH_RNG_COUNTER_BASED_ENGINE_HPP
#define MMS_MATH_RNG_COUNTER_BASED_ENGINE_HPP

/// \file 
/// CounterBasedEngine: turns a BlockFunction policy (Philox, Threefry) into a
/// std-style UniformRandomBitGenerator. Value semantics: a copy is a snapshot,
/// discard() is O(1).

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <math/rng/counter.hpp>
#include <math/rng/traits.hpp>

namespace mms::math::rng {

template <BlockFunction Block, unsigned Rounds>
class CounterBasedEngine {
public:
    using result_type = typename Block::result_type;
    using key_type = typename Block::key_type;
    using counter_type = Counter<Block::counter_words>;

    static constexpr std::size_t block_size = Block::word_count;

    constexpr CounterBasedEngine() noexcept = default;

    constexpr explicit CounterBasedEngine(key_type key, counter_type counter = {}) noexcept
        : key_{key}, counter_{counter} {}

    [[nodiscard]] static constexpr result_type min() noexcept {
        return 0;
    }
    [[nodiscard]] static constexpr result_type max() noexcept {
        return std::numeric_limits<result_type>::max();
    }

    constexpr void seed(key_type key, counter_type counter = {}) noexcept {
        key_ = key;
        counter_ = counter;
        cursor_ = block_size;
    }

    [[nodiscard]] constexpr result_type operator()() noexcept {
        if (cursor_ == block_size) {
            buffer_ = Block::block(counter_.words, key_, Rounds);
            counter_.bump();
            cursor_ = 0;
        }
        return buffer_[cursor_++];
    }

    constexpr void discard(unsigned long long z) noexcept {
        const unsigned long long avail = block_size - cursor_;
        if (z < avail) {
            cursor_ += static_cast<std::size_t>(z);
            return;
        }
        z -= avail;
        cursor_ = block_size;
        counter_.bump(static_cast<std::uint64_t>(z / block_size));
        if (const auto rem = static_cast<std::uint64_t>(z % block_size); rem != 0) {
            buffer_ = Block::block(counter_.words, key_, Rounds);
            counter_.bump();
            cursor_ = rem;
        }
    }

    [[nodiscard]] constexpr counter_type position() const noexcept {
        return counter_;
    }

    friend constexpr bool operator==(const CounterBasedEngine&, const CounterBasedEngine&) = default;

private:
    key_type key_{};
    counter_type counter_{};
    std::array<result_type, block_size> buffer_{};
    std::size_t cursor_ = block_size;
};

} // namespace mms::math::rng

#endif // MMS_MATH_RNG_COUNTER_BASED_ENGINE_HPP