#ifndef MMS_SIMULATION_AGENT_TIMING_HPP
#define MMS_SIMULATION_AGENT_TIMING_HPP

/// \file
/// How fast one agent lives: how often it looks at the market, how long its orders take
/// to reach the exchange, and how stale the market data it sees is. Two agents with the
/// same appetite for risk but different timing are two different market participants: a
/// broker watching every tick and a bot that checks in once a minute.

#include <simulation/delay_spec.hpp>

namespace mms::simulation {

/// Plain data, no invariant enforced here: validate_agent_setup checks it.
struct AgentTiming {
    /// The agent acts at the first tick where at least this much simulation time has
    /// passed since it last acted. Finite and > 0. A period shorter than dt means every
    /// tick.
    double decision_period = 1.0;

    /// How long each of its orders and cancels takes to reach the exchange. Drawn once
    /// per message from the agent's own latency stream.
    DelaySpec order_delay = ConstantDelaySpec{.delay = 0.0};

    /// How old the market data in its view is. Its own orders and positions are always
    /// current. Drawn each time it acts.
    DelaySpec info_delay = ConstantDelaySpec{.delay = 0.0};
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_AGENT_TIMING_HPP
