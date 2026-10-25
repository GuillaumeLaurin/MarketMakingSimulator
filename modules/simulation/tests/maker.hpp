#ifndef MMS_SIMULATION_TESTS_MAKER_HPP
#define MMS_SIMULATION_TESTS_MAKER_HPP

/// \file
/// Test scaffolding: a market maker as an AgentSpec, from the strategy it quotes with.

#include <string>
#include <utility>

#include <agents/market_maker.hpp>
#include <mm/strategy_config.hpp>
#include <mm/tick.hpp>

#include <simulation/agent_spec.hpp>
#include <simulation/delay_spec.hpp>
#include <simulation/size_spec.hpp>

namespace mms::simulation::tests {

/// A market maker with cash enough never to be the limit, the right to short, and a
/// decision period of one time unit (one tick, for the dt of 1 most tests use).
inline AgentSpec maker_agent(mm::StrategyConfig strategy,
                             mm::Ticks requote_threshold_ticks = 1,
                             DelaySpec order_delay = ConstantDelaySpec{.delay = 0.0},
                             double decision_period = 1.0,
                             std::string name = "maker") {
    return AgentSpec{
        .profile = {.name = std::move(name), .initial_cash = 1e12, .can_short = true},
        .timing = {.decision_period = decision_period, .order_delay = order_delay},
        .behavior = agents::MarketMakerSpec{.strategy = std::move(strategy),
                                            .requote_threshold_ticks = requote_threshold_ticks}};
}

/// Informed flow with the size of one occurrence uniform on 1..5, and cash enough never to be
/// the limit. Listed before the market maker, it reaches the exchange first at equal times.
inline AgentSpec informed_agent(double lambda,
                                double direction_correlation,
                                DelaySpec order_delay = ConstantDelaySpec{.delay = 0.0},
                                std::string name = "informed") {
    return AgentSpec{.profile = {.name = std::move(name), .initial_cash = 1e12, .can_short = true},
                     .timing = {.decision_period = 1.0, .order_delay = order_delay},
                     .behavior =
                         agents::InformedFlowSpec{.lambda = lambda,
                                                  .direction_correlation = direction_correlation,
                                                  .size = UniformIntSpec{.min = 1, .max = 5}}};
}

} // namespace mms::simulation::tests

#endif // MMS_SIMULATION_TESTS_MAKER_HPP
