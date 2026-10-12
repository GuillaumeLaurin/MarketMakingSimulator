#ifndef MMS_AGENTS_SIZING_HPP
#define MMS_AGENTS_SIZING_HPP

/// \file
/// How much an agent may trade without going past a position limit of its own.

#include <algorithm>

#include <mm/types.hpp>

namespace mms::agents {

/// The largest quantity, up to `size`, that a trade on `side` can take without leaving the
/// position past plus or minus `max_position`; zero when there is no room (the position is
/// at the limit on that side, or already past it). A trade the other way always has room, up
/// to `size`.
[[nodiscard]] constexpr mm::Quantity clip_to_limit(mm::Side side,
                                                   mm::Quantity position,
                                                   mm::Quantity size,
                                                   mm::Quantity max_position) noexcept {
    const mm::Quantity room =
        side == mm::Side::Buy ? max_position - position : max_position + position;
    return std::clamp<mm::Quantity>(room, 0, size);
}

} // namespace mms::agents

#endif // MMS_AGENTS_SIZING_HPP
