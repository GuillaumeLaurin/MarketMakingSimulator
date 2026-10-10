#ifndef MMS_SIMULATION_ASSET_SPEC_HPP
#define MMS_SIMULATION_ASSET_SPEC_HPP

/// \file
/// Everything about one asset that simulation itself needs, as opposed to
/// what mm::StrategyConfig already carries per asset (gamma's k, tick size,
/// quote size, ...) or what is a whole-simulation choice (PriceProcessSpec,
/// price_process_spec.hpp). World gathers these per-asset fields into the
/// Vector/Matrix math actually takes when it builds the one PriceProcess and
/// the one CovarianceMatrix.

#include <optional>
#include <string>

#include <mm/types.hpp>

#include <math/types.hpp>
#include <simulation/size_spec.hpp>

namespace mms::simulation {

/// Per-asset data FactorModel needs, present only when the simulation's
/// PriceProcessSpec is FactorModelSpec. beta has one entry per common
/// factor, so its size must match that spec's factor_count - checked by
/// whatever builds the process, not here.
struct FactorModelAssetData {
    math::Vector beta;
    double idiosyncratic_volatility;
};

/// One asset's own data. Plain aggregate, no invariant enforced here: every
/// field is validated wherever it is actually used (World building the
/// PriceProcess and the CovarianceMatrix, PnlLedger, adverse_flow).
struct AssetSpec {
    /// What this asset is called. It is the asset's identity: its random
    /// streams are derived from it (math::rng::asset_key_of), so renaming an
    /// asset, or reordering the basket, changes nothing about the others.
    /// Must be unique in a simulation; World checks.
    std::string symbol;

    /// The true price's starting point, also PnlLedger's initial mid.
    mm::Price initial_mid;

    /// Maker fee in basis points of notional, forwarded to LedgerAsset.
    double maker_fee_bps = 0.0;

    /// The true price process's own volatility, feeding World's
    /// CovarianceMatrix::from_correlation alongside every other asset's.
    /// Distinct from mm's per-asset volatility ESTIMATE, which only ever
    /// observes mids after the fact. In price units under Dynamics::Arithmetic,
    /// relative (a fraction of the price) under Dynamics::Geometric.
    double price_volatility;

    /// Drift of the true price, per unit of simulation time. In price units
    /// under Dynamics::Arithmetic; under Dynamics::Geometric the expected
    /// return rate, so E[price(t)] = initial_mid * exp(mu * t).
    double mu = 0.0;

    /// Present only when the simulation uses FactorModelSpec; ignored
    /// (and may be left empty) under RandomWalkSpec.
    std::optional<FactorModelAssetData> factor_model;

    /// Poisson arrival rate of adverse-flow occurrences on this asset, per
    /// unit of simulation time.
    double adverse_flow_lambda = 0.0;

    /// How adverse the direction of each occurrence is, in [0, 1]: with this
    /// probability an occurrence trades in the direction of the last price
    /// move (adverse selection), otherwise its direction is a fair coin.
    /// 0 is pure noise, 1 always follows the move. Validated where it is
    /// used (adverse_flow.cpp), not here.
    double adverse_flow_direction_correlation = 0.0;

    /// The size of one adverse-flow occurrence on this asset.
    DistributionSpec adverse_flow_size;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_ASSET_SPEC_HPP