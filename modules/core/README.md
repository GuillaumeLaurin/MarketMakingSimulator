# core

The exchange: an order book, a matching engine and a latency queue. It is generic over
the price and quantity types, and it knows nothing about strategies, prices or
simulations.

Depends on nothing in the project.

Start with `core/core.hpp` (the umbrella), then `core/matching_engine.hpp`. The book is
`core/order_book.hpp` and the delayed delivery of events is `core/latency_queue.hpp`.

Why it is built this way: [order book and matching](../../docs/architecture/order-book-and-matching.md).
