#ifndef MMS_AGENTS_TESTS_SCENE_HPP
#define MMS_AGENTS_TESTS_SCENE_HPP

/// \file
/// Shared by the tests of the agents: a Scene owns the buffers an AgentView points into, so
/// a test can describe a market in a few lines and hand the agent a view of it.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <agents/agent_view.hpp>
#include <mm/types.hpp>

#include <math/rng/stream.hpp>

namespace mms::agents::testing {

/// One asset's book and tape, owned.
struct Market {
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
    std::vector<TradePrint> trades;
    std::optional<mm::Price> signal;
    mm::Price tick_size = 0.01;
};

struct Scene {
    std::vector<Market> markets;
    std::vector<AssetView> assets;
    std::vector<mm::Quantity> positions;
    std::vector<OpenOrder> orders;
    std::vector<double> factors;
    double time = 1.0;
    double elapsed = 1.0;

    explicit Scene(std::size_t asset_count = 1) : markets(asset_count), positions(asset_count, 0) {}

    /// A market with a bid and an ask `half_spread` either side of `mid`.
    void quote(std::size_t asset, mm::Price mid, mm::Price half_spread = 0.05) {
        markets[asset].bids = {BookLevel{.price = mid - half_spread, .quantity = 100}};
        markets[asset].asks = {BookLevel{.price = mid + half_spread, .quantity = 100}};
    }

    [[nodiscard]] AgentView view() {
        assets.clear();
        for (const Market& market : markets) {
            assets.push_back(AssetView{.bids = market.bids,
                                       .asks = market.asks,
                                       .recent_trades = market.trades,
                                       .signal = market.signal,
                                       .tick_size = market.tick_size});
        }
        return AgentView{.tick = 1,
                         .time = time,
                         .elapsed = elapsed,
                         .account = {.cash = 1e9, .buying_power = 1e9, .positions = positions},
                         .open_orders = orders,
                         .assets = assets,
                         .factors = factors};
    }
};

inline math::rng::RngEngine engine(std::uint64_t seed = 1) {
    return math::rng::RngHub{math::rng::Seed{seed}}.engine_for(
        {.subsystem = math::rng::Subsystem::Agent, .asset = math::rng::no_asset});
}

} // namespace mms::agents::testing

#endif // MMS_AGENTS_TESTS_SCENE_HPP
