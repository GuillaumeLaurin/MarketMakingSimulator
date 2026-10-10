# agents

What trades in the market: the `Agent` interface, what an agent is shown and what it can ask
for, then the bots and the market maker as agents. It decides nothing about accounts or
timing. `simulation` holds the accounts, applies the rules and calls the agents.

Depends on `core`, `math` and `mm`. `simulation` will depend on it.

This module is being built. The interface comes first (`Agent`, `AgentView`, `Action`),
and the bots follow one at a time.

Why it is built this way: [agents and the human seat](../../docs/architecture/future/agents-and-human-seat.md).
