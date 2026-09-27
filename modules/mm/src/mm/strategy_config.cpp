/// \file
/// Implementation of make_strategy() (<mm/strategy_config.hpp>).

#include <mm/strategy_config.hpp>

#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <math/stats/volatility.hpp>
#include "mm/quoting_strategy.hpp"
#include <mm/avellaneda_stoikov_strategy.hpp>

namespace mms::mm {

namespace {

math::stats::AnyVolatilityEstimator to_estimator(const EstimatorSpec& spec) {
    return std::visit(
        [](const auto& concrete_spec) -> math::stats::AnyVolatilityEstimator {
            using Spec = std::decay_t<decltype(concrete_spec)>;
            if constexpr (std::is_same_v<Spec, ConstantSpec>) {
                return math::stats::AnyVolatilityEstimator{
                    math::stats::ConstantVolEstimator{concrete_spec.sigma}
                };
            } else if constexpr (std::is_same_v<Spec, EwmaSpec>) {
                return math::stats::AnyVolatilityEstimator{
                    math::stats::EwmaVolEstimator{concrete_spec.half_life}
                };
            } else {
                static_assert(std::is_same_v<Spec, RollingSpec>);
                return math::stats::AnyVolatilityEstimator{
                    math::stats::RollingVolEstimator{concrete_spec.window}
                };
            }
        },
    spec);
}

} // namespace

std::unique_ptr<QuotingStrategy> make_strategy(const StrategyConfig& config) {
    std::vector<math::stats::AnyVolatilityEstimator> volatility;
    volatility.reserve(config.estimators.size());
    for (const EstimatorSpec& spec : config.estimators) {
        volatility.push_back(to_estimator(spec));
    }

    return std::make_unique<AvellanedaStoikovStrategy>(
        AvellanedaStoikovSettings{
            .params = {.gamma = config.gamma, .k = config.k},
            .correlation = config.correlation,
            .volatility = std::move(volatility),
            .horizon = config.horizon,
            .quote_sizes = config.quote_sizes,
            .tick_sizes = config.tick_sizes,
            .limits = config.limits,
            .min_samples = config.min_samples
    });
}

} // namespace mms::mm