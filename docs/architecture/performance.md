# Performance

The simulator has to keep up with the clock. That gives it a budget, and the benchmarks
exist to tell how much of it is used.

## The budget

One tick is one second of market time. At real speed (1x), a tick has to be computed in
well under a second. At 100x, the same tick has 10 ms. The number of assets is what
spends it.

| Speed | Time available per tick |
|---|---|
| 1x | 1 s |
| 10x | 100 ms |
| 100x | 10 ms |

The budget is a way to read the results. It is not a test. Nothing fails if a benchmark
goes over it, and nothing in CI runs the benchmarks. I would rather look at the numbers
when I change something that could matter than gate merges on a timing from a shared
runner.

See [real time and threads](future/real-time-and-threads.md) for why one tick maps to
one second.

## What is measured

Three programs, one per layer, each in `modules/<name>/bench/`:

| Program | Question it answers |
|---|---|
| `math_process_step` | What does one price step cost for 10, 100, 1000 assets, with the dense random walk and with the factor model, arithmetic and geometric? |
| `core_matching_engine` | What does one order cost: resting and cancelling, taking one level, sweeping five, for books of 10, 100 and 1000 levels, with 0, 1 and 3 observers attached? |
| `simulation_runner_ticks` | How many ticks per second does a whole run manage with 1, 10 and 100 assets? |

The last one is the answer to the budget question. The first two say where the time goes
when it is too much.

The trading cases of the engine benchmark put back what they took, so the book keeps the
same depth from one iteration to the next. Their time includes that refill.

## How to run them

Build the `bench` preset, which is Release with benchmarks on. Debug builds are an order
of magnitude slower and tell you nothing.

```bash
cmake --preset bench
cmake --build --preset bench
build/bench/out/bench/simulation_runner_ticks
```

Use `bench-mingw` for MSYS2. On Windows with MinGW, put the directory with the DLLs on
the `PATH` first, or the program exits without printing anything:

```bash
PATH="$PWD/build/bench-mingw/out/bin:$PATH" build/bench-mingw/out/bench/simulation_runner_ticks.exe
```

## What the numbers do not cover

- The benchmark config is modest. Adverse flow is one arrival per asset per tick, books
  stay shallow, the strategy uses constant volatility estimators. A busier market costs
  more.
- Observers in the engine benchmark only count. An observer that does real work costs
  what that work costs.
- There is no application yet, so there is no thread handing snapshots to a screen, and
  that will cost too.
- It is one machine. Compare runs on the same machine.

No results are kept in the repository. They would only be worth something against a real
application, and a table of numbers ages faster than the code does.
