/// \file
/// Implementation of make_agent (<agents/agent_spec.hpp>).

#include <memory>
#include <type_traits>
#include <variant>

#include <agents/agent_spec.hpp>

namespace mms::agents {

namespace {

DeviationParams params_of(const TrendFollowerSpec& spec) {
    return DeviationParams{.half_life = spec.half_life,
                           .threshold = spec.threshold,
                           .order_size = spec.order_size,
                           .max_position = spec.max_position};
}

DeviationParams params_of(const MeanReverterSpec& spec) {
    return DeviationParams{.half_life = spec.half_life,
                           .threshold = spec.threshold,
                           .order_size = spec.order_size,
                           .max_position = spec.max_position};
}

} // namespace

std::unique_ptr<Agent> make_agent(const AgentBehavior& behavior) {
    return std::visit(
        [](const auto& spec) -> std::unique_ptr<Agent> {
            using Spec = std::decay_t<decltype(spec)>;
            if constexpr (std::is_same_v<Spec, MarketMakerSpec>) {
                return std::make_unique<MarketMakerAgent>(spec);
            } else if constexpr (std::is_same_v<Spec, InformedFlowSpec>) {
                return std::make_unique<InformedFlowAgent>(spec);
            } else if constexpr (std::is_same_v<Spec, NoiseTraderSpec>) {
                return std::make_unique<NoiseTraderAgent>(spec);
            } else if constexpr (std::is_same_v<Spec, TrendFollowerSpec>) {
                return std::make_unique<DeviationTraderAgent>(params_of(spec), Stance::Follow);
            } else if constexpr (std::is_same_v<Spec, MeanReverterSpec>) {
                return std::make_unique<DeviationTraderAgent>(params_of(spec), Stance::Fade);
            } else if constexpr (std::is_same_v<Spec, FactorTraderSpec>) {
                return std::make_unique<FactorTraderAgent>(spec);
            } else {
                static_assert(std::is_same_v<Spec, MarketMakerSpec>,
                              "an agent kind is not handled");
            }
        },
        behavior);
}

} // namespace mms::agents
