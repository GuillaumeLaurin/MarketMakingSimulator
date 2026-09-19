#ifndef MMS_CORE_LATENCY_QUEUE_HPP
#define MMS_CORE_LATENCY_QUEUE_HPP

/// \file
/// LatencyQueue<T>: a generic scheduled-delivery mechanism, not hardcoded
/// to orders - models the delay between "an event happens" and "it takes
/// effect/is observed" (order arrival, ack delivery, market data delivery,
/// or any other delayed-event kind). core never computes/decides the
/// latency VALUE itself (constant vs. sampled from a math::dist
/// distribution).

#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

namespace mms::core {

template<class T> class LatencyQueue {
public:
    /// Schedules payload for delivery at time `when` (seconds, matching
    /// PriceProcess::advance(double dt)'s existing convention elsewhere in
    /// the project - not another template parameter).
    void schedule(double when, T payload) {
        heap_.push(Entry{when, next_sequence_++, std::move(payload)});
    }

    /// Pops and returns everything scheduled at or before `now`, in
    /// scheduled-time order, ties broken by insertion order - a plain
    /// std::priority_queue is NOT tie-stable on its own, hence the explicit
    /// sequence number folded into the comparison key. Required for this
    /// project's bit-exact reproducibility goal.
    [[nodiscard]] std::vector<T> drain_until(double now) {
        std::vector<T> due;
        while (!heap_.empty() && heap_.top().when <= now) {
            due.push_back(std::move(const_cast<Entry&>(heap_.top()).payload));
            heap_.pop();
        }
        return due;
    }

    [[nodiscard]] bool empty() const noexcept { return heap_.empty(); }

    [[nodiscard]] std::size_t size() const noexcept { return heap_.size(); }

private:
    struct Entry {
        double when;
        std::uint64_t sequence;
        T payload;
    };

    /// Inverted relative to Entry's "natural" ordering, since
    /// std::priority_queue puts the comparator's "largest" element on top -
    /// this makes the SMALLEST (when, sequence) pair (earliest scheduled)
    /// come out first instead.
    struct Compare {
        [[nodiscard]] bool operator()(const Entry& a, const Entry& b) const noexcept {
            return (a.when != b.when) ? (a.when > b.when) : (a.sequence > b.sequence);
        }
    };

    std::priority_queue<Entry, std::vector<Entry>, Compare> heap_;
    std::uint64_t next_sequence_ = 0;
};

} // namespace mms::core

#endif // MMS_CORE_LATENCY_QUEUE_HPP