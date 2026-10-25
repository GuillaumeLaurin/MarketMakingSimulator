# Market making

The `mm` module holds everything a market maker decides and keeps track of. It does
not move time and it does not touch an exchange. The market maker agent in
[agents](agents.md) feeds it the true prices, asks it for quotes, and sends what changed.

```mermaid
flowchart LR
    view[MarketView: mids, inventory, time] --> strat[QuotingStrategy]
    strat --> quotes[desired quotes]
    quotes --> risk[risk limits]
    risk --> tick[round to the tick grid]
    tick --> plan[requote plan]
    live[live quotes] --> plan
    plan --> out[cancels and submits]
```

## The strategy

`QuotingStrategy` is an abstract class with one job: given a `MarketView`, return the
quotes to have in the market. The view carries the current mid of every asset, the
signed inventory as doubles, and the time. The conversion from quantity to double
happens once, when the view is built, so no strategy converts a quantity itself.

The two public entry points are not virtual. They check the contract the same way for
every strategy, and the protected virtuals do the work. A strategy is picked once from
a `StrategyConfig` by `make_strategy`, the only place that knows the concrete types.

### Avellaneda-Stoikov, for a portfolio

The one strategy so far. For a single asset, it quotes around a reservation price that
leans against inventory, with a spread that depends on risk aversion and volatility:

```
r         = mid - gamma * tau * sigma² * q
half      = (1 / gamma) * ln(1 + gamma / k)  +  gamma * tau * sigma² / 2
bid, ask  = r - half, r + half
```

`gamma` is risk aversion, `k` describes how fast order arrival falls with distance from
the mid, `tau` is the time horizon.

With several assets the `sigma² q` term becomes `Sigma q`, so holding one asset moves
the quotes on the correlated ones. `Sigma = D R D` is built from the volatilities the
strategy estimates and the configured correlation. It is applied as a product and the
matrix is never formed, which keeps it cheap.

### The strategy estimates its own volatility

It is not told the true volatility of the price process. That would be cheating, and
the gap between the two is part of what makes market making hard. Each asset gets an
estimator: a constant (for tests), an exponentially weighted one whose half-life is in
samples, or a rolling window. `min_samples` says how many observations are needed before
the strategy quotes at all.

### The horizon

`tau` comes from a policy. Either a constant, rolling horizon, or a finite one that
shrinks to a floor as the end of the session approaches. `math` takes `tau` as a plain
number and never decides what it means. That is a strategy choice and it lives in `mm`.

## Ticks

Quotes must sit on the asset's tick grid. A bid is rounded down and an ask rounded up,
so rounding only ever widens the spread and can never cross it. The bid and ask end up
at least one tick apart.

Doubles are not trusted to land on a grid line: 100.3 / 0.1 is 1002.9999999999999. So
the work is done on whole numbers of ticks, which compare and subtract exactly. The
requote threshold below needs that too.

## Risk limits

Two limits: a maximum absolute position per asset, and an optional cap on the portfolio
risk `qᵀ Sigma q`.

A breach does not throw. Being long or short is the normal state of a market maker. The
response is to stop quoting the side that would make things worse. Reducing exposure is
always allowed, even when already past the limit, so a position over the limit can still
get out.

## Requoting

Cancelling a quote that is still good throws away its place in the queue. So the plan
only replaces a quote if its price moved by at least `requote_threshold_ticks`, or if
its size changed. A threshold of 0 replaces every quote every tick.

`plan_requote` is pure. It takes the desired quotes, the live ones and the tick sizes,
and returns what to cancel and what to submit. It knows nothing about engines or clocks.
The market maker agent reads its live quotes from its own open orders and sends the plan as
actions.

## As an agent

`MarketMakerAgent` wraps a strategy and a requote threshold. Each time it acts it reads the
true price of every asset from the `signal` of its view, feeds it to the strategy, and sends
the plan. It keeps no record of its own orders. What it has in the market is read from the
open orders of its view, with one rule: a side that has an order on its way, or a cancel on
its way, is left alone until that has landed. A side with more than one resting order, which
should not happen, is cleaned up first, keeping the oldest.

It is the one agent that is handed the true price. It quotes around it, and nothing else in
the market could give it a price while it is the only one quoting. Its volatility is still
estimated, never read.

Two limits today. The state of the estimators is not part of what `save()` returns, so a run
resumed from a saved agent restarts them. And the first time it acts there is no earlier
price, so the estimators see their first sample without a return.

## Positions and P&L

A `Position` is a signed quantity at average cost. Long and short are both ordinary,
neither is an error. Adding to a position moves the average cost to the volume-weighted
mean, reducing it leaves the average cost alone and realises profit.

The `PnlLedger` owns one position per asset plus cash and fees, and explains profit in
two ways that always agree:

- Accounting: realised plus unrealised.
- Economic: spread captured at the moment of the fill, plus the inventory effect of the
  mid moving while a position is held.

That gives `realized + unrealized == spread_capture + inventory`, which is gross profit.
Fees are a separate bucket on top. Today only the maker fee exists, in basis points of
notional, and a negative value is a rebate. The ledger is fed by the account of the agent: `on_fill`
and `on_mark`.

Every agent has its own account, and the account has one ledger: the market maker, a bot and
later a person are all measured the same way. The account adds cash, reserves for pending
orders and collateral for shorts on top of it. See [agents](agents.md).
