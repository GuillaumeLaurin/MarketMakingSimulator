#ifndef MMS_AGENTS_FLOW_HPP
#define MMS_AGENTS_FLOW_HPP

/// \file
/// The random draws behind an agent that trades at random: how many orders arrive in a
/// stretch of time, which way each goes, functions of their
/// inputs and of the engine they are given, so each can be tested with nothing but an
/// RngEngine.
///
/// Draw order is fixed, which is what makes a run reproducible: first the arrival count,
/// then for each arrival its direction (always two engine words, even when the outcome
/// does not need both) and its size.

#include <cstdint>

#include <agents/agents_export.hpp>
#include <agents/size_spec.hpp>
#include <mm/types.hpp>

#include <math/rng/engine.hpp>

namespace mms::agents {

/// Number of arrivals in `dt` of time: Poisson with mean lambda * dt. Returns 0 without
/// drawing when lambda is 0. Throws std::invalid_argument unless lambda is finite and
/// >= 0, dt is finite and > 0, and lambda * dt is at most 1e6.
[[nodiscard]] MMS_AGENTS_EXPORT std::int64_t
draw_arrival_count(double lambda, double dt, math::rng::RngEngine& engine);

/// Direction of one arrival. With probability `correlation` it follows the sign of
/// `last_move` (up means buy, down means sell), otherwise it is a fair coin; a
/// `last_move` of exactly 0 has no direction to follow and also falls back to the coin.
/// So after an up move, P(Buy) is (1 + correlation) / 2. Throws std::invalid_argument
/// unless correlation is finite and in [0, 1] and last_move is finite.
[[nodiscard]] MMS_AGENTS_EXPORT mm::Side
draw_direction(double correlation, double last_move, math::rng::RngEngine& engine);

/// Size of one arrival, always >= 1. UniformIntSpec is uniform over [min, max];
/// ExponentialSpec is exponential with the given mean, rounded to the nearest whole share
/// and floored at 1. Throws std::invalid_argument for a UniformIntSpec with min < 1 or
/// min > max, or an ExponentialSpec whose mean is not finite and > 0.
[[nodiscard]] MMS_AGENTS_EXPORT mm::Quantity draw_size(const DistributionSpec& spec,
                                                       math::rng::RngEngine& engine);

/// The checks the draws above make on their inputs, for a caller that wants to reject a
/// bad spec before it has drawn anything. Each throws std::invalid_argument.
MMS_AGENTS_EXPORT void validate_correlation(double correlation);
MMS_AGENTS_EXPORT void validate_last_move(double last_move);
MMS_AGENTS_EXPORT void validate_size_spec(const DistributionSpec& spec);

} // namespace mms::agents

#endif // MMS_AGENTS_FLOW_HPP
