#ifndef MMS_MATH_TESTS_WORD_STUB_HPP
#define MMS_MATH_TESTS_WORD_STUB_HPP

/// \file
/// Deterministic uniform_random_bit_generator stubs for the dist tests. They let
/// a test pin the exact 64-bit words a sampler consumes, so a golden vector is a
/// pure function of hand-chosen input rather than of the real engine.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mms::math::testing {

/// Yields the same word on every call. constexpr and noexcept, so it drives
/// compile-time checks of the primitives and samplers.
struct FixedWord {
    using result_type = std::uint64_t;

    result_type value = 0;

    [[nodiscard]] static constexpr result_type min() noexcept { return 0; }
    [[nodiscard]] static constexpr result_type max() noexcept { return ~result_type{0}; }
    [[nodiscard]] constexpr result_type operator()() noexcept { return value; }
};

/// Replays a fixed list of words, then throws std::out_of_range. Tracks how many
/// words were consumed so a test can assert a sampler's exact word cost.
class WordStub {
public:
    using result_type = std::uint64_t;

    WordStub(std::initializer_list<std::uint64_t> words) : words_(words) {}
    explicit WordStub(std::vector<std::uint64_t> words) : words_(std::move(words)) {}

    [[nodiscard]] static constexpr result_type min() noexcept { return 0; }
    [[nodiscard]] static constexpr result_type max() noexcept { return ~result_type{0}; }

    result_type operator()() {
        if (cursor_ >= words_.size()) {
            throw std::out_of_range("WordStub: replay sequence exhausted");
        }
        return words_[cursor_++];
    }

    [[nodiscard]] std::size_t consumed() const noexcept { return cursor_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return words_.size() - cursor_; }

private:
    std::vector<std::uint64_t> words_;
    std::size_t cursor_ = 0;
};

} // namespace mms::math::testing

#endif // MMS_MATH_TESTS_WORD_STUB_HPP
