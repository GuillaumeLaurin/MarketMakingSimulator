#ifndef MMS_MATH_RNG_TRAITS_HPP
#define MMS_MATH_RNG_TRAITS_HPP

#include <array>
#include <concepts>
#include <cstdint>

namespace mms::math::rng {

template<class Block>
concept BlockFunction = requires(std::array<std::uint64_t, Block::counter_words> ctr,
                                 std::array<std::uint64_t, Block::key_words> key) {
    typename Block::result_type;
    { Block::word_count } -> std::convertible_to<std::size_t>;
    { Block::key_words } -> std::convertible_to<std::size_t>;
    { Block::counter_words } -> std::convertible_to<std::size_t>;
    { Block::block(ctr, key, 0U) };
};

} // namespace mms::math::rng

#endif // MMS_MATH_RNG_TRAITS_HPP
