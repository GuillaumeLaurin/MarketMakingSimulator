# math

Everything numeric: the random generator and its streams, the distribution samplers, the
price processes, the covariance matrix and its Cholesky factor, running statistics, and the
Avellaneda-Stoikov formulas. It knows nothing about markets.

Depends on Eigen, which appears in its public API.

Start with `math/math.hpp` (the umbrella). The parts, each with its own umbrella:
`math/rng.hpp`, `math/dist.hpp`, `math/process.hpp`, `math/covariance.hpp`,
`math/stats.hpp`. The shared vector and matrix types are in `math/types.hpp`.

Why it is built this way: [randomness](../../docs/architecture/randomness.md) and
[price processes](../../docs/architecture/price-processes.md).
