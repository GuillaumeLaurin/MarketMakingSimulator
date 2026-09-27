#ifndef MMS_MM_STRATEGY_CONFIG_HPP
#define MMS_MM_STRATEGY_CONFIG_HPP

/// \file
/// StrategyConfig: the plain data every quoting strategy is built from, and
/// make_strategy(), the one place that turns it into a live QuotingStrategy.
/// Only what constructing a strategy needs lives here.

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include <mm/horizon.hpp>
#include <mm/mm_export.hpp>
#include <mm/quoting_strategy.hpp>
#include <mm/risk.hpp>
#include <mm/types.hpp>

#include <math/types.hpp>

namespace mms::mm {

/// Ignores every observation; sigma is fixed at config.
struct ConstantSpec {
    double sigma;
};

/// Exponentially weighted; half_life is in NUMBER OF SAMPLES, matching
/// math::stats::EwmaEstimator (not a time unit).
struct EwmaSpec {
    double half_life;
};

/// Sample standard deviation over the last `window` samples.
struct RollingSpec {
    std::size_t window;
};

/// What kind of volatility estimator an asset gets. Converted to an
/// AnyVolatilityEstimator by make_strategy; the strategy itself only ever
/// sees the type-erased form.
using EstimatorSpec = std::variant<ConstantSpec, EwmaSpec, RollingSpec>;

/// Everything needed to build a quoting strategy. One entry per asset in
/// every per-asset vector (k, estimators, quote_sizes, tick_sizes,
/// limits.max_abs_position); their common size is the asset count. Plain
/// data, no invariant enforced here - make_strategy forwards it as-is and
/// the strategy's own constructor validates everything it actually uses.
struct StrategyConfig {
    double gamma;
    math::Vector k;
    math::Matrix correlation;
    std::vector<EstimatorSpec> estimators;
    HorizonPolicy horizon;
    std::vector<Quantity> quote_sizes;
    std::vector<Price> tick_sizes;
    RiskLimits limits;
    std::int64_t min_samples = 0;
};

/// Builds the configured strategy. Returns the QuotingStrategy base (MMS5:
/// the concrete strategy is a runtime choice), even though
/// AvellanedaStoikovStrategy is the only one that exists today. Throws
/// std::invalid_argument for whatever the concrete strategy's own
/// constructor rejects - this function performs no validation of its own.
[[nodiscard]] MMS_MM_EXPORT std::unique_ptr<QuotingStrategy>
make_strategy(const StrategyConfig& config);

} // namespace mms::mm

#endif // MMS_MM_STRATEGY_CONFIG_HPP