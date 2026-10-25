/// \file
/// Tests for agents/state_bytes.hpp: the bytes of a saved state that is a list of numbers.
/// What is checked is that numbers come back exactly as they went (including the ones that are
/// not ordinary: not-a-number, infinities, negative zero), and that anything that is not what
/// encode_doubles writes is refused.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include <agents/state_bytes.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

namespace agents = mms::agents;

constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();

} // namespace

TEST_CASE("Numbers come back exactly as they went", "[agents][state_bytes]") {
    const std::vector<double> values{1.5, -2.25, 0.0, 1e300, 5e-324, 100.0};

    const std::vector<double> back = agents::decode_doubles(agents::encode_doubles(values));

    REQUIRE(back.size() == values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        CHECK(back[i] == values[i]); // bit for bit: no rounding on the way
    }
}

TEST_CASE("Numbers that are not ordinary survive too", "[agents][state_bytes]") {
    const std::vector<double> values{quiet_nan, infinity, -infinity, -0.0};

    const std::vector<double> back = agents::decode_doubles(agents::encode_doubles(values));

    REQUIRE(back.size() == 4);
    CHECK(std::isnan(back[0]));
    CHECK(back[1] == infinity);
    CHECK(back[2] == -infinity);
    CHECK(back[3] == 0.0);
    CHECK(std::signbit(back[3]));
}

TEST_CASE("The bytes are a count and then the numbers", "[agents][state_bytes]") {
    const std::vector<double> values{3.0, 4.0, 5.0};

    const std::vector<std::byte> bytes = agents::encode_doubles(values);

    REQUIRE(bytes.size() == sizeof(std::uint64_t) + (3 * sizeof(double)));
    std::uint64_t count = 0;
    std::memcpy(&count, bytes.data(), sizeof(count));
    CHECK(count == 3);
}

TEST_CASE("Nothing is encoded as nothing", "[agents][state_bytes]") {
    const std::vector<double> none;

    CHECK_THROWS_AS(agents::encode_doubles(none), std::invalid_argument);
}

TEST_CASE("Bytes that are not an encoding are refused", "[agents][state_bytes]") {
    const std::vector<std::byte> good = agents::encode_doubles(std::vector<double>{1.0, 2.0});

    for (std::size_t length = 0; length < sizeof(std::uint64_t); ++length) {
        const std::vector<std::byte> too_short(length, std::byte{1});
        CHECK_THROWS_AS(agents::decode_doubles(too_short), std::invalid_argument);
    }

    std::vector<std::byte> truncated = good;
    truncated.pop_back();
    CHECK_THROWS_AS(agents::decode_doubles(truncated), std::invalid_argument);

    std::vector<std::byte> a_whole_number_too_many = good;
    a_whole_number_too_many.insert(a_whole_number_too_many.end(), sizeof(double), std::byte{0});
    CHECK_THROWS_AS(agents::decode_doubles(a_whole_number_too_many), std::invalid_argument);

    std::vector<std::byte> ragged = good;
    ragged.insert(ragged.end(), 3, std::byte{0});
    CHECK_THROWS_AS(agents::decode_doubles(ragged), std::invalid_argument);

    const std::vector<std::byte> bare_zero(sizeof(std::uint64_t), std::byte{0});
    CHECK_THROWS_AS(agents::decode_doubles(bare_zero), std::invalid_argument);

    std::vector<std::byte> zero_with_payload(sizeof(std::uint64_t), std::byte{0});
    zero_with_payload.insert(zero_with_payload.end(), sizeof(double), std::byte{0});
    CHECK_THROWS_AS(agents::decode_doubles(zero_with_payload), std::invalid_argument);

    std::vector<std::byte> huge(sizeof(std::uint64_t), std::byte{0xFF});
    huge.insert(huge.end(), sizeof(double), std::byte{0});
    CHECK_THROWS_AS(agents::decode_doubles(huge), std::invalid_argument);
}
