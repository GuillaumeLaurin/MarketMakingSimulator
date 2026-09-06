#ifndef MMS_MATH_DIST_PRIMITIVES_HPP
#define MMS_MATH_DIST_PRIMITIVES_HPP

/// \file
/// Raw engine-word to floating-point primitives shared by every sampler.
/// Each function consumes exactly one 64-bit engine word.

#include <concepts>
#include <cstdint>
#include <random>

namespace mms::math::dist {

/// A uniform_random_bit_generator that yields full-range 64-bit words, i.e. the
/// contract every RngEngine and every dist test stub must meet.
template<class G>
concept Uint64Source =
    std::uniform_random_bit_generator<G> && std::same_as<typename G::result_type, std::uint64_t> &&
    (G::min() == std::uint64_t{0}) && (G::max() == ~std::uint64_t{0});

/// One raw engine word.
template<Uint64Source G>
[[nodiscard]] constexpr std::uint64_t uniform_bits(G& g) noexcept(noexcept(g())) {
    return g();
}

/// Uniform on [0, 1) on a 2^-53 grid; every result is an exact dyadic.
template<Uint64Source G>
[[nodiscard]] constexpr double canonical_double(G& g) noexcept(noexcept(g())) {
    return static_cast<double>(g() >> 11) * 0x1.0p-53;
}

/// Uniform on (0, 1), strictly interior, on a 2^-52 grid; every result exact.
/// Feed this (never canonical_double) to probit and to -log for exponential.
template<Uint64Source G>
[[nodiscard]] constexpr double open_unit_double(G& g) noexcept(noexcept(g())) {
    return (static_cast<double>(g() >> 12) + 0.5) * 0x1.0p-52;
}

} // namespace mms::math::dist

#endif // MMS_MATH_DIST_PRIMITIVES_HPP