/// \file
/// Implementation of to_string(RejectReason) (<agents/reject_reason.hpp>).

#include <agents/reject_reason.hpp>

namespace mms::agents {

std::string_view to_string(RejectReason reason) noexcept {
    switch (reason) {
    case RejectReason::InsufficientCash:
        return "insufficient cash";
    case RejectReason::InsufficientCollateral:
        return "insufficient collateral";
    case RejectReason::ShortNotAllowed:
        return "short selling not allowed";
    case RejectReason::PositionLimit:
        return "position limit";
    case RejectReason::SelfTrade:
        return "self trade";
    case RejectReason::UnknownAsset:
        return "unknown asset";
    case RejectReason::InvalidOrder:
        return "invalid order";
    }
    return "unknown";
}

} // namespace mms::agents