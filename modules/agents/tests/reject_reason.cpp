/// \file
/// Tests for agents/reject_reason.hpp: every reason has its own short name, the
/// names are stable (they end up in logs and in the counts of rejects), and a
/// value outside the enumeration still gets a name instead of a crash.

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string_view>
#include <type_traits>

#include <agents/reject_reason.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

using mms::agents::RejectReason;
using mms::agents::to_string;

constexpr std::array<RejectReason, 6> all_reasons{
    RejectReason::InsufficientCash,
    RejectReason::InsufficientCollateral,
    RejectReason::ShortNotAllowed,
    RejectReason::PositionLimit,
    RejectReason::UnknownAsset,
    RejectReason::InvalidOrder,
};

} // namespace

static_assert(std::is_same_v<std::underlying_type_t<RejectReason>, std::uint8_t>);

TEST_CASE("Each reject reason has its own stable name", "[agents][reject_reason]") {
    CHECK(to_string(RejectReason::InsufficientCash) == "insufficient cash");
    CHECK(to_string(RejectReason::InsufficientCollateral) == "insufficient collateral");
    CHECK(to_string(RejectReason::ShortNotAllowed) == "short selling not allowed");
    CHECK(to_string(RejectReason::PositionLimit) == "position limit");
    CHECK(to_string(RejectReason::UnknownAsset) == "unknown asset");
    CHECK(to_string(RejectReason::InvalidOrder) == "invalid order");
}

TEST_CASE("No two reject reasons share a name, and none is empty", "[agents][reject_reason]") {
    std::set<std::string_view> names;
    for (const RejectReason reason : all_reasons) {
        const std::string_view name = to_string(reason);
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown");
        names.insert(name);
    }
    CHECK(names.size() == all_reasons.size());
}

TEST_CASE("A value outside the enumeration is named 'unknown'", "[agents][reject_reason]") {
    CHECK(to_string(static_cast<RejectReason>(200)) == "unknown");
}

TEST_CASE("The reasons are numbered from zero, in a fixed order", "[agents][reject_reason]") {
    // The order is not a promise to callers, but the counts of rejects are kept in an
    // array indexed by it, so a reorder must be a conscious change.
    std::uint8_t expected = 0;
    for (const RejectReason reason : all_reasons) {
        CHECK(static_cast<std::uint8_t>(reason) == expected);
        ++expected;
    }
}

TEST_CASE("reject_reason_count is the number of reasons: the last one is the one before it",
          "[agents][reject_reason]") {
    using mms::agents::RejectReason;
    CHECK(mms::agents::reject_reason_count ==
          static_cast<std::size_t>(RejectReason::InvalidOrder) + 1);
}
