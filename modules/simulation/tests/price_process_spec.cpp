/// \file
/// Tests for simulation/price_process_spec.hpp: RandomWalkSpec carries no
/// data (it is a pure selector), FactorModelSpec holds its factor_count
/// exactly, and the variant holds whichever alternative it was built from.
/// This is a whole-SIMULATION choice (SIM6/SIM18 correction), so there is no
/// per-asset angle to test here - that lives in asset_spec.hpp.

#include <cstddef>
#include <type_traits>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include <simulation/price_process_spec.hpp>

namespace sim = mms::simulation;

TEST_CASE("RandomWalkSpec carries no data", "[simulation][price_process_spec]") {
    STATIC_REQUIRE(std::is_empty_v<sim::RandomWalkSpec>);
}

TEST_CASE("FactorModelSpec holds its factor_count exactly", "[simulation][price_process_spec]") {
    constexpr sim::FactorModelSpec spec{.factor_count = 3};

    CHECK(spec.factor_count == 3);
}

TEST_CASE("FactorModelSpec::factor_count is a std::size_t, not a signed or narrower type",
          "[simulation][price_process_spec]") {
    STATIC_REQUIRE(std::is_same_v<decltype(sim::FactorModelSpec::factor_count), std::size_t>);
}

TEST_CASE("PriceProcessSpec holds whichever alternative it was built from",
          "[simulation][price_process_spec]") {
    const sim::PriceProcessSpec random_walk = sim::RandomWalkSpec{};
    const sim::PriceProcessSpec factor_model = sim::FactorModelSpec{.factor_count = 2};

    REQUIRE(std::holds_alternative<sim::RandomWalkSpec>(random_walk));
    REQUIRE(std::holds_alternative<sim::FactorModelSpec>(factor_model));
    CHECK(std::get<sim::FactorModelSpec>(factor_model).factor_count == 2);
}

TEST_CASE("PriceProcessSpec has exactly the two documented alternatives, RandomWalkSpec first",
          "[simulation][price_process_spec]") {
    STATIC_REQUIRE(std::variant_size_v<sim::PriceProcessSpec> == 2);
    STATIC_REQUIRE(
        std::is_same_v<std::variant_alternative_t<0, sim::PriceProcessSpec>, sim::RandomWalkSpec>);
}

TEST_CASE("PriceProcessSpec defaults to RandomWalkSpec when default-constructed",
          "[simulation][price_process_spec]") {
    // std::variant's own rule (first alternative, default-constructed), worth
    // pinning since it decides what an unconfigured PriceProcessSpec means.
    const sim::PriceProcessSpec default_spec;

    CHECK(std::holds_alternative<sim::RandomWalkSpec>(default_spec));
}
