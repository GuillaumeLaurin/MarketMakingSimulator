#ifndef MMS_SIMULATION_MARKET_HISTORY_HPP
#define MMS_SIMULATION_MARKET_HISTORY_HPP

/// \file
/// MarketHistory: what the market looked like, kept long enough to show it to agents. Each
/// tick the loop records a snapshot (the best levels of every book and the true price of
/// every asset), and the trades that happened since the previous one go on a tape. An agent
/// is then shown the snapshot that was current `delay` ago, and the trades it has not seen yet.
///
/// Two lengths are kept apart because they cost very differently. Snapshots hold whole book
/// sides, and an agent only ever looks back as far as its information delay. The tape holds a
/// few numbers per trade, and an agent that acts rarely has to be shown everything since it
/// last looked.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <utility>
#include <vector>

#include <agents/agent_view.hpp>
#include <mm/types.hpp>

#include <simulation/simulation_export.hpp>

namespace mms::simulation {

class MMS_SIMULATION_EXPORT MarketHistory {
public:
    /// `tick_sizes` has one entry per asset. `snapshots_kept` and `ticks_of_tape_kept` are at
    /// least 1 (a smaller value is raised to it); `trade_limit` is the most trades an agent is
    /// shown per asset. Throws std::invalid_argument for a tick size that is not finite and > 0.
    MarketHistory(std::vector<mm::Price> tick_sizes,
                  std::size_t snapshots_kept,
                  std::size_t ticks_of_tape_kept,
                  std::size_t trade_limit);

    /// A trade of `asset` happened. It joins the tape at the next record().
    void add_trade(mm::AssetId asset, const agents::TradePrint& print);

    /// A new tick starts at `time`: the market is now as given, one entry per asset, and the
    /// trades added since the last call are put on the tape under this tick. Times must be
    /// increasing. Throws std::invalid_argument for entries that are not one per asset.
    void record(double time,
                std::span<const double> prices,
                std::vector<std::vector<agents::BookLevel>> bids,
                std::vector<std::vector<agents::BookLevel>> asks);

    /// The market as the newest snapshot that is not newer than `target_time` showed it, or
    /// the oldest kept when there is none that old, with the trades since the tick `seen`
    /// (which is updated). `signal` says whether the true prices are shown. The spans are valid
    /// until the next call of view() or record(). Throws std::logic_error before the first
    /// record().
    [[nodiscard]] std::span<const agents::AssetView>
    view(double target_time, bool signal, std::uint64_t& seen);

    /// How many snapshots and how many ticks of tape are kept right now.
    [[nodiscard]] std::size_t snapshot_count() const noexcept { return snapshots_.size(); }

    [[nodiscard]] std::size_t tape_ticks() const noexcept { return tape_.size(); }

private:
    struct Snapshot {
        double time;
        std::uint64_t serial;
        std::vector<double> prices;
        std::vector<std::vector<agents::BookLevel>> bids;
        std::vector<std::vector<agents::BookLevel>> asks;
    };

    struct Printed {
        mm::AssetId asset;
        agents::TradePrint print;
    };

    struct TapeTick {
        std::uint64_t serial;
        std::vector<Printed> prints;
    };

    std::vector<mm::Price> tick_sizes_;
    std::size_t snapshots_kept_;
    std::size_t tape_kept_;
    std::size_t trade_limit_;
    std::uint64_t serial_ = 0;
    std::deque<Snapshot> snapshots_;
    std::deque<TapeTick> tape_;
    std::vector<Printed> pending_;
    std::vector<std::vector<agents::TradePrint>> scratch_; ///< what view() hands out
    std::vector<agents::AssetView> views_;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_MARKET_HISTORY_HPP
