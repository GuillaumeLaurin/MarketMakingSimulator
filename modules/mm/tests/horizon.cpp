/// \file
/// Tests for mm/horizon.hpp: the constant and finite tau policies, the
/// floor that keeps a finite horizon from ever going negative (a negative tau
/// would flip the sign of Avellaneda-Stoikov's inventory skew, and math does
/// not check it), dispatch through the std::variant, and validation, which
/// must reject a default-constructed policy rather than quote with tau = 0.

#include <limits>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mm/horizon.hpp>

namespace mm = mms::mm;

namespace {

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

mm::HorizonPolicy constant(double tau) {
    return mm::ConstantHorizon{.tau = tau};
}

mm::HorizonPolicy finite_horizon(double end_time, double min_tau = 0.0) {
    return mm::FiniteHorizon{.end_time = end_time, .min_tau = min_tau};
}

} // namespace

TEST_CASE("A constant horizon returns the same tau at every instant", "[mm][horizon]") {
    const mm::HorizonPolicy policy = constant(2.5);

    CHECK(mm::tau_at(policy, 0.0) == 2.5);
    CHECK(mm::tau_at(policy, 40.0) == 2.5);
    CHECK(mm::tau_at(policy, 1.0e6) == 2.5);
    CHECK(mm::tau_at(policy, -5.0) == 2.5);
}

TEST_CASE("A finite horizon shrinks linearly to the end time", "[mm][horizon]") {
    const mm::HorizonPolicy policy = finite_horizon(100.0);

    CHECK(mm::tau_at(policy, 0.0) == 100.0);
    CHECK(mm::tau_at(policy, 40.0) == 60.0);
    CHECK(mm::tau_at(policy, 99.5) == 0.5);
}

TEST_CASE("A finite horizon reaches exactly its floor at the end time", "[mm][horizon]") {
    CHECK(mm::tau_at(finite_horizon(100.0), 100.0) == 0.0);
    CHECK(mm::tau_at(finite_horizon(100.0, 5.0), 100.0) == 5.0);
}

TEST_CASE("A finite horizon never goes negative after the end time", "[mm][horizon]") {
    CHECK(mm::tau_at(finite_horizon(100.0), 150.0) == 0.0);
    CHECK(mm::tau_at(finite_horizon(100.0, 5.0), 150.0) == 5.0);
    CHECK(mm::tau_at(finite_horizon(100.0), 1.0e9) == 0.0);
}

TEST_CASE("The floor holds once the remaining time drops below it", "[mm][horizon]") {
    const mm::HorizonPolicy policy = finite_horizon(100.0, 5.0);

    CHECK(mm::tau_at(policy, 90.0) == 10.0); // above the floor: the remaining time
    CHECK(mm::tau_at(policy, 95.0) == 5.0);  // exactly at the floor
    CHECK(mm::tau_at(policy, 99.0) == 5.0);  // below it: held at the floor
    CHECK(mm::tau_at(policy, 200.0) == 5.0);
}

TEST_CASE("A floor larger than the whole horizon applies from the start", "[mm][horizon]") {
    CHECK(mm::tau_at(finite_horizon(10.0, 20.0), 0.0) == 20.0);
}

TEST_CASE("A finite horizon accepts a time before zero", "[mm][horizon]") {
    CHECK(mm::tau_at(finite_horizon(100.0), -10.0) == 110.0);
}

TEST_CASE("A finite horizon is never negative and never grows as time advances", "[mm][horizon]") {
    const mm::HorizonPolicy policy = finite_horizon(100.0);
    std::vector<double> violations;
    double previous = mm::tau_at(policy, -10.0);

    for (int step = 0; step <= 420; ++step) {
        const double time = -10.0 + (step * 0.5);
        const double tau = mm::tau_at(policy, time);
        if (tau < 0.0 || tau > previous) {
            violations.push_back(time);
        }
        previous = tau;
    }
    CHECK(violations.empty());
}

TEST_CASE("tau_at dispatches on whichever alternative the policy holds", "[mm][horizon]") {
    mm::HorizonPolicy policy = mm::ConstantHorizon{.tau = 3.0};
    CHECK(mm::tau_at(policy, 50.0) == 3.0);

    policy = mm::FiniteHorizon{.end_time = 100.0};
    CHECK(mm::tau_at(policy, 50.0) == 50.0);

    policy = mm::ConstantHorizon{.tau = 3.0};
    CHECK(mm::tau_at(policy, 50.0) == 3.0);
}

TEST_CASE("A default-constructed policy is rejected instead of quoting with tau = 0",
          "[mm][horizon]") {
    const mm::HorizonPolicy policy;

    CHECK_THROWS_AS(mm::tau_at(policy, 0.0), std::invalid_argument);
}

TEST_CASE("A constant horizon rejects a tau that is not finite and positive", "[mm][horizon]") {
    for (const double bad : {0.0, -1.0, quiet_nan, infinity, -infinity}) {
        CHECK_THROWS_AS(mm::tau_at(constant(bad), 0.0), std::invalid_argument);
    }
}

TEST_CASE("A constant horizon accepts a tiny positive tau", "[mm][horizon]") {
    CHECK(mm::tau_at(constant(1.0e-12), 0.0) == 1.0e-12);
}

TEST_CASE("A finite horizon rejects a non-finite end time", "[mm][horizon]") {
    for (const double bad : {quiet_nan, infinity, -infinity}) {
        CHECK_THROWS_AS(mm::tau_at(finite_horizon(bad), 0.0), std::invalid_argument);
    }
}

TEST_CASE("A finite horizon rejects a negative or non-finite floor", "[mm][horizon]") {
    for (const double bad : {-0.001, quiet_nan, infinity, -infinity}) {
        CHECK_THROWS_AS(mm::tau_at(finite_horizon(100.0, bad), 0.0), std::invalid_argument);
    }
}

TEST_CASE("A zero floor is valid", "[mm][horizon]") {
    CHECK_NOTHROW(mm::tau_at(finite_horizon(100.0, 0.0), 0.0));
}

TEST_CASE("Both policies reject a non-finite time", "[mm][horizon]") {
    for (const double bad : {quiet_nan, infinity, -infinity}) {
        CHECK_THROWS_AS(mm::tau_at(constant(2.0), bad), std::invalid_argument);
        CHECK_THROWS_AS(mm::tau_at(finite_horizon(100.0), bad), std::invalid_argument);
    }
}
