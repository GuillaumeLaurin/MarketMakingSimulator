/// \file
/// Implementation of World (<simulation/world.hpp>).

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <mm/strategy_config.hpp>

#include <math/covariance/covariance_matrix.hpp>
#include <math/process/correlated_random_walk.hpp>
#include <math/process/factor_model.hpp>
#include <math/rng/stream.hpp>
#include <simulation/world.hpp>

#include "math/process/price_process.hpp"
#include "math/rng/engine.hpp"
#include "math/types.hpp"
#include "mm/pnl.hpp"
#include "simulation/asset_spec.hpp"
#include "simulation/price_process_spec.hpp"
#include "simulation/simulation_config.hpp"

namespace mms::simulation {

namespace {

[[noreturn]] void reject(const char* message) {
    throw std::invalid_argument(std::string("World: ") + message);
}

math::rng::AssetKey asset_key(std::size_t asset) {
    return math::rng::AssetKey{static_cast<std::uint64_t>(asset)};
}

std::vector<mm::LedgerAsset> gather_ledger_assets(const SimulationConfig& config) {
    std::vector<mm::LedgerAsset> assets;
    assets.reserve(config.assets.size());
    for (const AssetSpec& asset : config.assets) {
        assets.push_back(mm::LedgerAsset{.initial_mid = asset.initial_mid,
                                         .maker_fee_bps = asset.maker_fee_bps});
    }
    return assets;
}

std::vector<math::rng::RngEngine> build_engines(const SimulationConfig& config,
                                                math::rng::Subsystem subsystem) {
    const math::rng::RngHub hub(config.master_seed);
    std::vector<math::rng::RngEngine> engines;
    engines.reserve(config.assets.size());
    for (std::size_t i = 0; i < config.assets.size(); ++i) {
        engines.push_back(hub.engine_for({.subsystem = subsystem, .asset = asset_key(i)}));
    }
    return engines;
}

std::unique_ptr<math::process::PriceProcess> build_price_process(const SimulationConfig& config) {
    const std::vector<AssetSpec>& assets = config.assets;
    if (assets.empty()) {
        reject("at least one asset is required");
    }
    const auto n = static_cast<Eigen::Index>(assets.size());
    if (config.correlation.rows() != n || config.correlation.cols() != n) {
        reject("correlation must be N x N, N the number of assets");
    }

    std::vector<double> vols;
    vols.reserve(assets.size());
    for (const AssetSpec& asset : assets) {
        vols.push_back(asset.price_volatility);
    }
    const math::covariance::CovarianceMatrix cov =
        math::covariance::CovarianceMatrix::from_correlation(vols, config.correlation);

    const math::rng::RngHub hub(config.master_seed);

    math::Vector initial_prices(n);
    math::Vector mu(n);
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const auto index = static_cast<Eigen::Index>(i);
        initial_prices(index) = assets[i].initial_mid;
        mu(index) = assets[i].mu;
    }

    return std::visit(
        [&](const auto& process_spec) -> std::unique_ptr<math::process::PriceProcess> {
            using Spec = std::decay_t<decltype(process_spec)>;

            if constexpr (std::is_same_v<Spec, RandomWalkSpec>) {
                std::vector<math::rng::RngEngine> engines;
                engines.reserve(assets.size());
                for (std::size_t i = 0; i < assets.size(); ++i) {
                    if (assets[i].factor_model.has_value()) {
                        reject("an asset has factor_model data but the process is RandomWalkSpec");
                    }
                    engines.push_back(hub.engine_for(
                        {.subsystem = math::rng::Subsystem::Price, .asset = asset_key(i)}));
                }
                return std::make_unique<math::process::CorrelatedRandomWalk>(
                    initial_prices, mu, cov.cholesky(), std::move(engines));
            } else {
                static_assert(std::is_same_v<Spec, FactorModelSpec>);
                const auto k = static_cast<Eigen::Index>(process_spec.factor_count);

                math::Matrix beta(n, k);
                math::Vector idio_vol(n);
                std::vector<math::rng::RngEngine> price_engines;
                price_engines.reserve(assets.size());
                for (std::size_t i = 0; i < assets.size(); ++i) {
                    if (!assets[i].factor_model.has_value()) {
                        reject("an asset is missing factor_model data for FactorModelSpec");
                    }
                    const FactorModelAssetData& data = *assets[i].factor_model;
                    if (data.beta.size() != k) {
                        reject("an asset's beta size does not match factor_count");
                    }
                    const auto index = static_cast<Eigen::Index>(i);
                    beta.row(index) = data.beta.transpose();
                    idio_vol(index) = data.idiosyncratic_volatility;
                    price_engines.push_back(hub.engine_for(
                        {.subsystem = math::rng::Subsystem::Price, .asset = asset_key(i)}));
                }

                std::vector<math::rng::RngEngine> factor_engines;
                factor_engines.reserve(process_spec.factor_count);
                for (std::size_t f = 0; f < process_spec.factor_count; ++f) {
                    factor_engines.push_back(hub.engine_for({
                        .subsystem = math::rng::Subsystem::PriceFactor,
                        .asset = asset_key(f),
                    }));
                }

                return std::make_unique<math::process::FactorModel>(initial_prices,
                                                                    mu,
                                                                    beta,
                                                                    math::Matrix::Identity(k, k),
                                                                    idio_vol,
                                                                    std::move(price_engines),
                                                                    std::move(factor_engines));
            }
        },
        config.process);
}

} // namespace

World::World(const SimulationConfig& config)
    : price_process_(build_price_process(config)), engines_(config.assets.size()), event_queue_(),
      ledger_(gather_ledger_assets(config)), strategy_(mm::make_strategy(config.strategy)),
      adverse_flow_engines_(build_engines(config, math::rng::Subsystem::AdverseFlow)),
      latency_engines_(build_engines(config, math::rng::Subsystem::Latency)) {
    if (strategy_->asset_count() != config.assets.size()) {
        reject("strategy's per-asset vectors do not agree with the number of assets");
    }
}

} // namespace mms::simulation