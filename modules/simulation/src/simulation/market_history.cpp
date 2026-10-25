/// \file
/// Implementation of MarketHistory (<simulation/market_history.hpp>).

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <simulation/market_history.hpp>

namespace mms::simulation {

MarketHistory::MarketHistory(std::vector<mm::Price> tick_sizes,
                             std::size_t snapshots_kept,
                             std::size_t ticks_of_tape_kept,
                             std::size_t trade_limit)
    : tick_sizes_(std::move(tick_sizes)), snapshots_kept_(std::max<std::size_t>(snapshots_kept, 1)),
      tape_kept_(std::max<std::size_t>(ticks_of_tape_kept, 1)), trade_limit_(trade_limit),
      scratch_(tick_sizes_.size()), views_(tick_sizes_.size()) {
    for (const mm::Price tick_size : tick_sizes_) {
        if (!std::isfinite(tick_size) || tick_size <= 0.0) {
            throw std::invalid_argument("MarketHistory: a tick size must be finite and > 0");
        }
    }
}

void MarketHistory::add_trade(mm::AssetId asset, const agents::TradePrint& print) {
    pending_.push_back(Printed{.asset = asset, .print = print});
}

void MarketHistory::record(double time,
                           std::span<const double> prices,
                           std::vector<std::vector<agents::BookLevel>> bids,
                           std::vector<std::vector<agents::BookLevel>> asks) {
    const std::size_t assets = tick_sizes_.size();
    if (prices.size() != assets || bids.size() != assets || asks.size() != assets) {
        throw std::invalid_argument("MarketHistory: the record must have one entry per asset");
    }
    ++serial_;
    snapshots_.push_back(Snapshot{.time = time,
                                  .serial = serial_,
                                  .prices = std::vector<double>(prices.begin(), prices.end()),
                                  .bids = std::move(bids),
                                  .asks = std::move(asks)});
    tape_.push_back(TapeTick{.serial = serial_, .prints = std::move(pending_)});
    pending_.clear();
    while (snapshots_.size() > snapshots_kept_) {
        snapshots_.pop_front();
    }
    while (tape_.size() > tape_kept_) {
        tape_.pop_front();
    }
}

std::span<const agents::AssetView>
MarketHistory::view(double target_time, bool signal, std::uint64_t& seen) {
    if (snapshots_.empty()) {
        throw std::logic_error("MarketHistory: there is nothing recorded yet");
    }

    // The newest snapshot that is old enough; the oldest kept when none is.
    const Snapshot* chosen = &snapshots_.front();
    for (auto it = snapshots_.rbegin(); it != snapshots_.rend(); ++it) {
        if (it->time <= target_time) {
            chosen = &*it;
            break;
        }
    }

    for (std::vector<agents::TradePrint>& prints : scratch_) {
        prints.clear();
    }
    for (const TapeTick& tick : tape_) {
        if (tick.serial <= seen || tick.serial > chosen->serial) {
            continue;
        }
        for (const Printed& printed : tick.prints) {
            scratch_[printed.asset].push_back(printed.print);
        }
    }
    seen = std::max(seen, chosen->serial);

    for (std::size_t asset = 0; asset < views_.size(); ++asset) {
        std::vector<agents::TradePrint>& prints = scratch_[asset];
        if (prints.size() > trade_limit_) {
            prints.erase(prints.begin(), prints.end() - static_cast<std::ptrdiff_t>(trade_limit_));
        }
        views_[asset] = agents::AssetView{
            .bids = chosen->bids[asset],
            .asks = chosen->asks[asset],
            .recent_trades = prints,
            .signal = signal ? std::optional<mm::Price>(chosen->prices[asset]) : std::nullopt,
            .tick_size = tick_sizes_[asset]};
    }
    return views_;
}

} // namespace mms::simulation
