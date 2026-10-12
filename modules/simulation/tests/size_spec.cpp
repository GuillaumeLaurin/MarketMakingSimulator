/// \file
/// The size specs live in MMS::agents (agents/size_spec.hpp, where they are tested); the
/// names MMS::simulation keeps for them must be the very same types, so that a config built
/// with either spelling is the same config.

#include <type_traits>
#include <variant>

#include <agents/size_spec.hpp>
#include <catch2/catch_test_macros.hpp>

#include <simulation/adverse_flow.hpp>
#include <simulation/size_spec.hpp>

namespace sim = mms::simulation;
namespace agents = mms::agents;

static_assert(std::is_same_v<sim::UniformIntSpec, agents::UniformIntSpec>);
static_assert(std::is_same_v<sim::ExponentialSpec, agents::ExponentialSpec>);
static_assert(std::is_same_v<sim::DistributionSpec, agents::DistributionSpec>);

TEST_CASE("The simulation spellings of the size specs are the agents' types",
          "[simulation][size_spec]") {
    const sim::DistributionSpec from_simulation = sim::UniformIntSpec{.min = 1, .max = 3};
    const agents::DistributionSpec from_agents = from_simulation;

    REQUIRE(std::holds_alternative<agents::UniformIntSpec>(from_agents));
    CHECK(std::get<agents::UniformIntSpec>(from_agents).max == 3);
}

TEST_CASE("The simulation spellings of the draws are the agents' functions",
          "[simulation][adverse_flow]") {
    // The same function: the address is the same.
    CHECK(&sim::draw_size == &agents::draw_size);
}
