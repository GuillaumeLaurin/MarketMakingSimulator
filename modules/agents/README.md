# agents

What trades in the market: the `Agent` interface, what an agent is shown (`AgentView`) and
what it can ask for (`Action`), the market maker as an agent, and the bots. It decides
nothing about accounts or timing. `simulation` holds the accounts, applies the rules of the
exchange and calls the agents.

Depends on `core`, `math` and `mm`. `simulation` depends on it.

Start with `agents/agents.hpp` (the umbrella), then `agents/agent.hpp` and
`agents/agent_view.hpp`. An agent kind is described by a spec (`MarketMakerSpec`,
`InformedFlowSpec`, `NoiseTraderSpec`, `TrendFollowerSpec`, `MeanReverterSpec`,
`FactorTraderSpec`) and built by `make_agent` in `agents/agent_spec.hpp`, the one place that
knows the concrete types.

Why it is built this way: [agents](../../docs/architecture/agents.md). What is still to come,
the seat for a person: [agents and the human seat](../../docs/architecture/future/agents-and-human-seat.md).
