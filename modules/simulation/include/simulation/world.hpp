#ifndef MMS_SIMULATION_WORLD_HPP
#define MMS_SIMULATION_WORLD_HPP

/// \file
/// World: everything a run advances, built once from a SimulationConfig and
/// then driven tick by tick by tick_loop.cpp.
/// ONE math::process::PriceProcess for the whole correlated basket (never
/// one per asset: a single process object advances every asset's true price
/// jointly), one mm::MatchingEngine per asset, one portfolio-wide
/// mm::LatencyQueue for quote submit/cancel requests, one portfolio
/// mm::PnlLedger, and one portfolio mm::QuotingStrategy. Builds the
/// CovarianceMatrix once at construction and uses it to derive
/// whatever the chosen PriceProcess needs.

#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

#include <mm/pnl.hpp>
#include <mm/quoting_strategy.hpp>
#include <mm/requote.hpp>
#include <mm/types.hpp>

#include <core/latency_queue.hpp>
#include <math/process/price_process.hpp>
#include <math/rng/engine.hpp>
#include <simulation/simulation_config.hpp>
#include <simulation/simulation_export.hpp>

namespace mms::simulation {

/// One adverse-flow order on its way to the exchange of one asset.
struct AdverseOrder {
    mm::AssetId asset;
    mm::MarketOrder order;
};

/// Everything that can be in flight: the market maker's own requests and
/// adverse flow. One shared queue gives a single, time-ordered view of all
/// of it (SIM27).
using SimulationEvent = std::variant<mm::SubmitRequest, mm::CancelRequest, AdverseOrder>;

using EventQueue = core::LatencyQueue<SimulationEvent>;

class MMS_SIMULATION_EXPORT World {
public:
    /// Builds everything from `config`: the CovarianceMatrix, the one
    /// PriceProcess, one MatchingEngine per asset, the PnlLedger, the
    /// strategy, and one RngEngine per asset for adverse flow and for
    /// latency. Throws std::invalid_argument for anything inconsistent:
    /// `config.assets` empty or its size disagreeing with `correlation`'s
    /// dimension or `config.strategy`'s per-asset vectors; an asset without a
    /// symbol, or two assets with the same symbol; a
    /// FactorModelSpec process with an asset missing `factor_model`, or a
    /// RandomWalkSpec process with one present; a correlation that is not
    /// symmetric positive definite; anything the strategy's or the
    /// PriceProcess's own constructor rejects.
    explicit World(const SimulationConfig& config);

    [[nodiscard]] std::size_t asset_count() const noexcept { return engines_.size(); }

    [[nodiscard]] math::process::PriceProcess& price_process() noexcept { return *price_process_; }

    [[nodiscard]] const math::process::PriceProcess& price_process() const noexcept {
        return *price_process_;
    }

    [[nodiscard]] mm::MatchingEngine& matching_engine(mm::AssetId asset) {
        return engines_.at(asset);
    }

    [[nodiscard]] const mm::MatchingEngine& matching_engine(mm::AssetId asset) const {
        return engines_.at(asset);
    }

    [[nodiscard]] mm::PnlLedger& pnl_ledger() noexcept { return ledger_; }

    [[nodiscard]] const mm::PnlLedger& pnl_ledger() const noexcept { return ledger_; }

    [[nodiscard]] mm::QuotingStrategy& strategy() noexcept { return *strategy_; }

    [[nodiscard]] const mm::QuotingStrategy& strategy() const noexcept { return *strategy_; }

    /// The one queue of everything still on its way to an exchange: the
    /// market maker's quote submits and cancels and the adverse-flow orders,
    /// all assets together, released in (due time, insertion order) order.
    [[nodiscard]] EventQueue& event_queue() noexcept { return event_queue_; }

    /// This asset's own RngEngine for drawing adverse-flow occurrences
    /// (arrival count, direction, size). Persists across ticks: the caller
    /// must hold this reference, not re-derive it, so the stream actually
    /// advances.
    [[nodiscard]] math::rng::RngEngine& adverse_flow_engine(mm::AssetId asset) {
        return adverse_flow_engines_.at(asset);
    }

    /// This asset's own RngEngine for drawing message delays: quote
    /// submit/cancel latency and adverse-flow latency share this one
    /// stream (math::rng::Subsystem only has one Latency key per asset).
    [[nodiscard]] math::rng::RngEngine& latency_engine(mm::AssetId asset) {
        return latency_engines_.at(asset);
    }

private:
    std::unique_ptr<math::process::PriceProcess> price_process_;
    std::vector<mm::MatchingEngine> engines_;
    EventQueue event_queue_;
    mm::PnlLedger ledger_;
    std::unique_ptr<mm::QuotingStrategy> strategy_;
    std::vector<math::rng::RngEngine> adverse_flow_engines_;
    std::vector<math::rng::RngEngine> latency_engines_;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_WORLD_HPP