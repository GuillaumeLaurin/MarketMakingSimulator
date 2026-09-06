/// \file
/// Tests for the Sampler concept. A handful of minimal mock types pin exactly
/// which shapes the concept accepts and which it rejects. Everything is decided
/// at compile time; the TEST_CASE only surfaces the results to CTest.

#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <math/dist/primitives.hpp>
#include <math/dist/traits.hpp>

namespace {

using mms::math::dist::Sampler;
using mms::math::dist::Uint64Source;

// --- well-formed samplers -----------------------------------------------------

struct DoubleSampler {
    using result_type = double;
    static constexpr int words_per_sample = 1;

    result_type operator()(Uint64Source auto& g) const {
        return mms::math::dist::canonical_double(g);
    }
};

struct IntSampler {
    using result_type = std::int64_t;
    static constexpr int words_per_sample = 1;

    result_type operator()(Uint64Source auto& g) const {
        return static_cast<std::int64_t>(mms::math::dist::uniform_bits(g));
    }
};

struct BoolSampler {
    using result_type = bool;
    static constexpr int words_per_sample = 1;

    result_type operator()(Uint64Source auto& g) const {
        return mms::math::dist::canonical_double(g) < 0.5;
    }
};

struct VariableCostSampler {
    using result_type = double;
    static constexpr int words_per_sample = -1; // rejection based

    result_type operator()(Uint64Source auto& g) const {
        return mms::math::dist::canonical_double(g);
    }
};

// --- malformed samplers -----------------------------------------------------

struct NoWordCount {
    using result_type = double;

    result_type operator()(Uint64Source auto& g) const {
        return mms::math::dist::canonical_double(g);
    }
};

struct WrongReturnType {
    using result_type = double;
    [[maybe_unused]] static constexpr int words_per_sample = 1;

    int operator()(Uint64Source auto& g) const { // not result_type
        return static_cast<int>(mms::math::dist::uniform_bits(g));
    }
};

struct NonArithmeticResult {
    using result_type = std::string;
    [[maybe_unused]] static constexpr int words_per_sample = 1;

    result_type operator()(Uint64Source auto& /*g*/) const { return {}; }
};

struct NotCallable {
    using result_type = double;
    [[maybe_unused]] static constexpr int words_per_sample = 1;
};

// --- the contract -----------------------------------------------------------

static_assert(Sampler<DoubleSampler>);
static_assert(Sampler<IntSampler>);
static_assert(Sampler<BoolSampler>);
static_assert(Sampler<VariableCostSampler>,
              "the sign of words_per_sample is not part of the concept");

static_assert(!Sampler<NoWordCount>, "words_per_sample is required");
static_assert(!Sampler<WrongReturnType>, "operator() must return exactly result_type");
static_assert(!Sampler<NonArithmeticResult>, "result_type must be arithmetic");
static_assert(!Sampler<NotCallable>, "a Sampler must be callable with an engine");

} // namespace

TEST_CASE("Sampler accepts well-formed samplers and rejects malformed ones", "[dist][traits]") {
    STATIC_REQUIRE(Sampler<DoubleSampler>);
    STATIC_REQUIRE(Sampler<IntSampler>);
    STATIC_REQUIRE(Sampler<BoolSampler>);
    STATIC_REQUIRE(Sampler<VariableCostSampler>);

    STATIC_REQUIRE_FALSE(Sampler<NoWordCount>);
    STATIC_REQUIRE_FALSE(Sampler<WrongReturnType>);
    STATIC_REQUIRE_FALSE(Sampler<NonArithmeticResult>);
    STATIC_REQUIRE_FALSE(Sampler<NotCallable>);
}
