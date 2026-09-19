/// \file
/// Tests for LatencyQueue<T>: scheduled-time ordering, insertion-order
/// tie-breaking (the determinism property this project's reproducibility
/// goal depends on - a plain std::priority_queue is not tie-stable), the
/// inclusive "at or before now" boundary, and move-only payload support.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <core/latency_queue.hpp>

using mms::core::LatencyQueue;

TEST_CASE("LatencyQueue starts empty", "[core][latency_queue]") {
    LatencyQueue<int> queue;

    CHECK(queue.empty());
    CHECK(queue.size() == 0);
    CHECK(queue.drain_until(100.0).empty());
}

TEST_CASE("LatencyQueue::schedule grows the queue", "[core][latency_queue]") {
    LatencyQueue<int> queue;
    queue.schedule(1.0, 10);
    queue.schedule(2.0, 20);

    CHECK_FALSE(queue.empty());
    CHECK(queue.size() == 2);
}

TEST_CASE("LatencyQueue::drain_until returns only what is due, leaving the rest queued",
          "[core][latency_queue]") {
    LatencyQueue<int> queue;
    queue.schedule(1.0, 10);
    queue.schedule(2.0, 20);
    queue.schedule(3.0, 30);

    const std::vector<int> due = queue.drain_until(2.0);

    CHECK(due == std::vector<int>{10, 20});
    CHECK(queue.size() == 1); // the t=3.0 entry is still waiting
}

TEST_CASE("LatencyQueue::drain_until is inclusive at exactly `now`", "[core][latency_queue]") {
    LatencyQueue<int> queue;
    queue.schedule(5.0, 42);

    CHECK(queue.drain_until(4.999).empty());
    CHECK(queue.drain_until(5.0) == std::vector<int>{42});
}

TEST_CASE("LatencyQueue::drain_until returns entries in scheduled-time order regardless of "
          "insertion order",
          "[core][latency_queue]") {
    LatencyQueue<int> queue;
    queue.schedule(3.0, 30); // inserted first, but scheduled last
    queue.schedule(1.0, 10);
    queue.schedule(2.0, 20);

    CHECK(queue.drain_until(10.0) == std::vector<int>{10, 20, 30});
}

TEST_CASE("LatencyQueue breaks ties on identical scheduled times by insertion order (FIFO)",
          "[core][latency_queue]") {
    LatencyQueue<std::string> queue;
    queue.schedule(1.0, "first");
    queue.schedule(1.0, "second");
    queue.schedule(1.0, "third");

    // A plain std::priority_queue could return these in ANY order; the
    // explicit sequence number folded into the comparison key is what
    // guarantees insertion order here.
    CHECK(queue.drain_until(1.0) == std::vector<std::string>{"first", "second", "third"});
}

TEST_CASE("LatencyQueue breaks ties by insertion order even with many equal-time entries",
          "[core][latency_queue]") {
    // With only 3-4 entries a plain std::priority_queue happens to return
    // equal keys in insertion order by accident of its binary-heap layout,
    // which would let a comparator with NO sequence tiebreak pass the small
    // tests above. Enough equal-time entries, interleaved with a few
    // distinct times to force sift-ups/sift-downs, reliably scrambles
    // that accident - so this is the test that actually pins the sequence
    // number's necessity.
    LatencyQueue<int> queue;
    constexpr int n = 200;
    for (int i = 0; i < n; ++i) {
        queue.schedule(1.0, i);            // all tied at t=1.0, payload = insertion index
        queue.schedule(0.5 + (i % 3), -1); // noise at other times, filtered out below
    }

    std::vector<int> tied;
    for (const int v : queue.drain_until(10.0)) {
        if (v >= 0) {
            tied.push_back(v);
        }
    }

    REQUIRE(tied.size() == n);
    for (int i = 0; i < n; ++i) {
        CHECK(tied[static_cast<std::size_t>(i)] == i); // strictly increasing insertion order
    }
}

TEST_CASE("LatencyQueue tie-breaking holds across interleaved distinct times",
          "[core][latency_queue]") {
    LatencyQueue<std::string> queue;
    queue.schedule(2.0, "b1");
    queue.schedule(1.0, "a1");
    queue.schedule(2.0, "b2");
    queue.schedule(1.0, "a2");

    CHECK(queue.drain_until(2.0) == std::vector<std::string>{"a1", "a2", "b1", "b2"});
}

TEST_CASE("LatencyQueue::drain_until with nothing due returns empty and leaves the queue intact",
          "[core][latency_queue]") {
    LatencyQueue<int> queue;
    queue.schedule(10.0, 1);

    CHECK(queue.drain_until(5.0).empty());
    CHECK(queue.size() == 1);
}

TEST_CASE("LatencyQueue successive drains never return the same entry twice",
          "[core][latency_queue]") {
    LatencyQueue<int> queue;
    queue.schedule(1.0, 10);
    queue.schedule(2.0, 20);

    CHECK(queue.drain_until(1.0) == std::vector<int>{10});
    CHECK(queue.drain_until(1.0).empty()); // already drained
    CHECK(queue.drain_until(2.0) == std::vector<int>{20});
    CHECK(queue.empty());
}

TEST_CASE("LatencyQueue supports move-only payloads", "[core][latency_queue]") {
    LatencyQueue<std::unique_ptr<int>> queue;
    queue.schedule(1.0, std::make_unique<int>(7));
    queue.schedule(2.0, std::make_unique<int>(8));

    const std::vector<std::unique_ptr<int>> due = queue.drain_until(2.0);

    REQUIRE(due.size() == 2);
    CHECK(*due[0] == 7);
    CHECK(*due[1] == 8);
    CHECK(queue.empty());
}
