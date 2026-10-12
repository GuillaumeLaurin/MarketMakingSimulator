#ifndef MMS_AGENTS_DEVIATION_TRADER_HPP
#define MMS_AGENTS_DEVIATION_TRADER_HPP

/// \file
/// The trend follower and the mean reverter: one mechanism, two opposite stances.
///
/// On each asset the agent keeps an exponentially weighted average of the price it reads off
/// the book (agents/book.hpp), updated once per act: `half_life` is counted in acts, so an
/// average with a half-life of 10 has forgotten half of what it saw 10 acts ago. At each act it
/// compares the price with the average as it stood before: the relative deviation
/// `price / average - 1`. When that is above `threshold` the price is running up, and when it is
/// below minus `threshold` it is running down. Nothing in between.
///
///   Follow (TrendFollowerSpec)  buys a rise and sells a fall.
///   Fade   (MeanReverterSpec)   sells a rise and buys a fall.
///
/// A trade is one market order of `order_size` shares, cut down so that the position never goes
/// past plus or minus `max_position`. The first price it sees on an asset only starts the
/// average, and an asset with no price to read is left alone (and its average with it).
///
/// It draws no random number. The averages are its state and are saved.
///
/// Its orders are subject to the rules of the account like anyone's: it needs cash for the
/// shares it buys, and the right to sell short and collateral for the ones it sells beyond
/// what it holds.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <agents/agent.hpp>
#include <agents/agents_export.hpp>
#include <mm/types.hpp>

namespace mms::agents {

enum class Stance : std::uint8_t {
    Follow, ///< trades with the deviation: the trend follower
    Fade    ///< trades against it: the mean reverter
};

/// What the two stances have in common.
struct DeviationParams {
    /// Half-life of the average, in acts. Finite and > 0.
    double half_life;
    /// Relative deviation from the average above which it trades. Finite and >= 0.
    double threshold;
    /// Shares per trade. At least 1.
    mm::Quantity order_size;
    /// Largest position it will hold, long or short. At least 1.
    mm::Quantity max_position;
};

/// The trend follower. The fields mean what they do in DeviationParams.
struct TrendFollowerSpec {
    double half_life;
    double threshold;
    mm::Quantity order_size;
    mm::Quantity max_position;
};

/// The mean reverter. The fields mean what they do in DeviationParams.
struct MeanReverterSpec {
    double half_life;
    double threshold;
    mm::Quantity order_size;
    mm::Quantity max_position;
};

class MMS_AGENTS_EXPORT DeviationTraderAgent final : public Agent {
public:
    /// Throws std::invalid_argument for a half-life that is not finite and > 0, a threshold
    /// that is not finite and >= 0, or an order size or a position limit below 1.
    DeviationTraderAgent(const DeviationParams& params, Stance stance);

    /// Throws std::invalid_argument when the number of assets changes from one act to the
    /// next, or the view does not have one position per asset.
    void act(const AgentView& view, math::rng::RngEngine& rng, std::vector<Action>& out) override;

    /// The average of each asset, as bytes of this machine. Empty before the first act.
    [[nodiscard]] std::vector<std::byte> save() const override;

    /// Restores what save() produced. Throws std::invalid_argument for bytes that are not
    /// that: nothing is changed then.
    void load(std::span<const std::byte> state) override;

private:
    DeviationParams params_;
    Stance stance_;
    double weight_;               ///< share of a new price in the average, from the half-life
    std::vector<double> average_; ///< per asset; NaN until the first price is seen
};

} // namespace mms::agents

#endif // MMS_AGENTS_DEVIATION_TRADER_HPP
