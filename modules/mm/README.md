# mm

What a market maker decides and keeps track of: the quoting strategy, the risk limits, the
requote plan, positions and the P&L ledger. It does not move time and it does not touch an
exchange. `simulation` calls it.

Depends on `core` and `math`.

Start with `mm/mm.hpp` (the umbrella), then `mm/quoting_strategy.hpp` for the interface and
`mm/avellaneda_stoikov_strategy.hpp` for the one strategy. `mm/types.hpp` is where the
concrete price and quantity types are chosen.

Why it is built this way: [market making](../../docs/architecture/market-making.md).
