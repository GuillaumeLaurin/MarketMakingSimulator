#ifndef MMS_SIMULATION_PRICE_PROCESS_SPEC_HPP
#define MMS_SIMULATION_PRICE_PROCESS_SPEC_HPP

/// \file
/// Which price-process mechanism drives the whole simulated basket. One
/// choice for the entire run, never per asset: a single math::process
/// object advances every asset's price jointly in one call (its correlation
/// structure spans the whole basket), so mixing mechanisms across assets
/// would silently lose correlation between the two groups. Per-asset data
/// the chosen mechanism needs (drift, and for FactorModel the asset's beta
/// row and idiosyncratic volatility) lives in AssetSpec; World gathers it
/// into the Vector/Matrix math actually takes when it builds the one
/// PriceProcess.

#include <cstddef>
#include <variant>

namespace mms::simulation {

/// CorrelatedRandomWalk: cross-asset correlation carried entirely by the
/// global covariance's Cholesky factor. No extra data beyond what AssetSpec
/// and the global correlation already provide.
struct RandomWalkSpec {};

/// FactorModel: correlation carried by `factor_count` common factors plus a
/// per-asset idiosyncratic term. The factors themselves are drawn as
/// independent standard normals (no factor-to-factor correlation): nothing
/// in this project has needed correlated factors yet, and FactorModel's own
/// rationale is already "cheaper than full Sigma", which correlated factors
/// would partly undo.
struct FactorModelSpec {
    std::size_t factor_count;
};

/// The whole-simulation choice of price-process mechanism.
using PriceProcessSpec = std::variant<RandomWalkSpec, FactorModelSpec>;

} // namespace mms::simulation

#endif // MMS_SIMULATION_PRICE_PROCESS_SPEC_HPP