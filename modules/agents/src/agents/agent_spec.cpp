/// \file
/// Implementation of make_agent (<agents/agent_spec.hpp>).

#include <memory>
#include <type_traits>
#include <variant>

#include <agents/agent_spec.hpp>

namespace mms::agents {

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
            } else {
                static_assert(std::is_same_v<Spec, MarketMakerSpec>,
                              "an agent kind is not handled");
            }
        },
        behavior);
}

} // namespace mms::agents
