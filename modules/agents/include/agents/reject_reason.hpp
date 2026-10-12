#ifndef MMS_AGENTS_REJECT_REASON_HPP
#define MMS_AGENTS_REJECT_REASON_HPP

/// \file
/// Why the gateway refused an action.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <agents/agents_export.hpp>

namespace mms::agents {

enum class RejectReason : std::uint8_t {
    InsufficientCash,       ///< a buy the agent cannot pay for
    InsufficientCollateral, ///< a short sale it cannot cover
    ShortNotAllowed,        ///< a short sale from an agent that may not short
    PositionLimit,          ///< it would take the position past its limit
    UnknownAsset,           ///< no such asset
    InvalidOrder,           ///< a non-positive quantity or price, or a price off the grid
};

/// How many reasons there are: the size of a table of counts indexed by reason. A reason is
/// added at the end, and this goes up with it.
inline constexpr std::size_t reject_reason_count = 6;

/// A short stable name, for logs and for counting rejects.
[[nodiscard]] MMS_AGENTS_EXPORT std::string_view to_string(RejectReason reason) noexcept;

} // namespace mms::agents

#endif // MMS_AGENTS_REJECT_REASON_HPP