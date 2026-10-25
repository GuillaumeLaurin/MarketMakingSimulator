/// \file
/// Implementation of the state encoding (<agents/state_bytes.hpp>).

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <agents/state_bytes.hpp>

namespace mms::agents {

std::vector<std::byte> encode_doubles(std::span<const double> values) {
    if (values.empty()) {
        throw std::invalid_argument("encode_doubles: there is nothing to encode");
    }
    const std::uint64_t count = values.size();
    std::vector<std::byte> bytes(sizeof(count) + (values.size() * sizeof(double)));
    std::memcpy(bytes.data(), &count, sizeof(count));
    std::memcpy(bytes.data() + sizeof(count), values.data(), values.size() * sizeof(double));
    return bytes;
}

std::vector<double> decode_doubles(std::span<const std::byte> bytes) {
    std::uint64_t count = 0;
    if (bytes.size() < sizeof(count)) {
        throw std::invalid_argument("decode_doubles: the saved state is too short");
    }
    std::memcpy(&count, bytes.data(), sizeof(count));
    const std::size_t payload = bytes.size() - sizeof(count);
    if (count == 0 || payload % sizeof(double) != 0 || payload / sizeof(double) != count) {
        throw std::invalid_argument("decode_doubles: the saved state does not hold a whole number "
                                    "of values");
    }
    std::vector<double> values(count);
    std::memcpy(values.data(), bytes.data() + sizeof(count), count * sizeof(double));
    return values;
}

} // namespace mms::agents
