#ifndef MMS_AGENTS_STATE_BYTES_HPP
#define MMS_AGENTS_STATE_BYTES_HPP

/// \file
/// The bytes of an agent's saved state, for an agent whose state is a list of numbers (the
/// last signal of each asset, the running average of each price). Agent::save() and
/// Agent::load() move opaque bytes; this is the one encoding the agents that need it share.
///
/// A count (a 64-bit unsigned integer) followed by that many doubles, in the byte order of
/// this machine: a state is meant to be read back by the same build, not carried to another.

#include <cstddef>
#include <span>
#include <vector>

#include <agents/agents_export.hpp>

namespace mms::agents {

/// The bytes that stand for `values`. `values` must not be empty (an agent with nothing to
/// remember saves no bytes at all).
[[nodiscard]] MMS_AGENTS_EXPORT std::vector<std::byte>
encode_doubles(std::span<const double> values);

/// The numbers `bytes` stands for. Throws std::invalid_argument for anything encode_doubles
/// did not write: too short to hold a count, a count of zero, or a size that is not exactly
/// that many doubles.
[[nodiscard]] MMS_AGENTS_EXPORT std::vector<double>
decode_doubles(std::span<const std::byte> bytes);

} // namespace mms::agents

#endif // MMS_AGENTS_STATE_BYTES_HPP
