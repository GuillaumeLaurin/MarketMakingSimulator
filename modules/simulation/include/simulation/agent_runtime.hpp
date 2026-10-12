#ifndef MMS_SIMULATION_AGENT_RUNTIME_HPP
#define MMS_SIMULATION_AGENT_RUNTIME_HPP

/// \file
/// AgentRuntime: one agent as the loop holds it. The agent itself, its account, the list of
/// its orders, its decision clock, its two random streams and the factor moves gathered for
/// it. It is also the gateway: the actions an agent asks for are checked against its account
/// here, and what is accepted comes out as a message to deliver to the exchange at a given
/// time. The runtime knows no matching engine and no queue; the loop owns those, which is
/// what lets everything below be tested without running a market.
///
/// The life of an order, as the loop drives it:
///
///   act()          the agent decides; each action is refused (on_reject is called and the
///                  refusal is returned) or accepted: a ticket holds back its cash, the
///                  order is on its way, and a SubmitArrival / CancelArrival is returned
///                  with the time it reaches the exchange
///   on_arrived()   the order reached the exchange and rests
///   on_fill()      part of it traded
///   on_done()      it ended without trading what was left: cancelled, or an immediate
///                  order that did not rest
///
/// Randomness is drawn the same way whatever happens: one delay per action asked for, before
/// the action is judged, so that a refusal never shifts a later draw.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

#include <agents/action.hpp>
#include <agents/agent.hpp>
#include <agents/agent_view.hpp>
#include <agents/reject_reason.hpp>
#include <mm/pnl.hpp>
#include <mm/types.hpp>

#include <math/rng/engine.hpp>
#include <simulation/account.hpp>
#include <simulation/account_rules.hpp>
#include <simulation/agent_id.hpp>
#include <simulation/agent_orders.hpp>
#include <simulation/agent_spec.hpp>
#include <simulation/simulation_export.hpp>

namespace mms::simulation {

/// An accepted order on its way to the exchange.
struct SubmitArrival {
    AgentId agent;
    Ticket ticket;
    std::variant<agents::SubmitLimit, agents::SubmitMarket> order;
};

/// A cancel on its way to the exchange.
struct CancelArrival {
    AgentId agent;
    mm::AssetId asset;
    mm::OrderId id;
};

using AgentEvent = std::variant<SubmitArrival, CancelArrival>;

/// A message and the simulation time at which it reaches the exchange.
struct Scheduled {
    double due;
    AgentEvent event;
};

/// An action the gateway refused.
struct Refusal {
    agents::Action action;
    agents::RejectReason reason;
};

/// What came out of one act(). Actions that are neither (a Cancel of an order that is not
/// resting) are dropped silently: cancelling an order that has gone is not an error.
struct ActResult {
    std::vector<Scheduled> scheduled;
    std::vector<Refusal> refused;
};

class MMS_SIMULATION_EXPORT AgentRuntime {
public:
    /// Builds the agent from `spec.behavior` and its account from `spec.profile`, over the
    /// `assets` the world gives its own ledger. `id` is the agent's position in the list.
    /// `decision` and `latency` are its random streams, owned from here on. Throws what
    /// make_agent and Account reject (std::invalid_argument).
    AgentRuntime(AgentId id,
                 const AgentSpec& spec,
                 const AccountRules& rules,
                 const std::vector<mm::LedgerAsset>& assets,
                 math::rng::RngEngine decision,
                 math::rng::RngEngine latency);

    /// The same with an agent that is already built, in place of the one `spec.behavior`
    /// describes (which is then ignored). For tests, and for the day an agent comes from
    /// somewhere other than the closed list of behaviors.
    AgentRuntime(AgentId id,
                 const AgentSpec& spec,
                 const AccountRules& rules,
                 const std::vector<mm::LedgerAsset>& assets,
                 math::rng::RngEngine decision,
                 math::rng::RngEngine latency,
                 std::unique_ptr<agents::Agent> agent);

    [[nodiscard]] AgentId id() const noexcept { return id_; }

    [[nodiscard]] const AgentSpec& spec() const noexcept { return spec_; }

    [[nodiscard]] const Account& account() const noexcept { return account_; }

    [[nodiscard]] const AgentOrders& orders() const noexcept { return orders_; }

    [[nodiscard]] const agents::Agent& agent() const noexcept { return *agent_; }

    /// Is it time for the agent to act? The first time it is at once. After that, once
    /// decision_period has passed since it last acted (a relative 1e-9 slack keeps a
    /// period of 0.3 from missing a tick because of rounding).
    [[nodiscard]] bool due(double time) const noexcept;

    /// The moves of the common factors since the last time the agent acted are added up
    /// here, for the agents that are shown them. The first call fixes how many factors
    /// there are. Throws std::invalid_argument when a later call has another size.
    void add_factors(std::span<const double> increments);

    /// Tells the agent to act on `markets` (one per asset) and sends what it asks for
    /// through the gateway. The time elapsed since its last act, its account, its orders
    /// and the factors gathered for it are filled in here; `markets` is the loop's.
    /// Sets the clock: the agent has now acted, the factors start again from zero.
    /// Throws what the agent throws.
    [[nodiscard]] ActResult
    act(std::int64_t tick, double time, std::span<const agents::AssetView> markets);

    /// The order of `ticket` reached the exchange and rests under `id`. Throws
    /// std::out_of_range for an unknown ticket.
    void on_arrived(Ticket ticket, mm::OrderId id);

    /// The order of `ticket` traded. Settles the account, shrinks the order and tells the
    /// agent. Throws what Account::on_fill and AgentOrders::on_filled throw.
    void on_fill(Ticket ticket, const agents::Fill& fill);

    /// The order of `ticket` is over without trading what was left of it. Frees what it
    /// held back. Returns false for a ticket that is already closed, which is not an error.
    bool on_done(Ticket ticket);

private:
    /// Judges one action: nothing, a refusal or a scheduled message.
    void send(const agents::Action& action, double now, double delay, ActResult& result);

    AgentId id_;
    AgentSpec spec_;
    std::unique_ptr<agents::Agent> agent_;
    Account account_;
    AgentOrders orders_;
    math::rng::RngEngine decision_;
    math::rng::RngEngine latency_;
    double last_act_ = 0.0;
    double next_due_ = 0.0;
    std::vector<double> factors_;
    std::vector<mm::Quantity> positions_; ///< what the view's account.positions points at
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_AGENT_RUNTIME_HPP
