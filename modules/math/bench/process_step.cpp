/// \file
/// What one price step costs, by asset count: a raw RNG draw, a step of
/// CorrelatedRandomWalk (a dense N x N product, O(N^2)) and a step of
/// FactorModel (O(N k)). Run on demand: build/<preset>/out/bench/math_process_step.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nanobench.h>

#include <math/dist/normal.hpp>
#include <math/process/correlated_random_walk.hpp>
#include <math/process/dynamics.hpp>
#include <math/process/factor_model.hpp>
#include <math/rng/stream.hpp>
#include <math/types.hpp>

namespace {

namespace rng = mms::math::rng;
using mms::math::Index;
using mms::math::Matrix;
using mms::math::Vector;
using mms::math::process::CorrelatedRandomWalk;
using mms::math::process::Dynamics;
using mms::math::process::FactorModel;

constexpr double dt = 1.0;
constexpr std::size_t factor_count = 5;

std::vector<rng::RngEngine>
engines_for(const rng::RngHub& hub, rng::Subsystem subsystem, std::size_t n) {
    std::vector<rng::RngEngine> engines;
    engines.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        engines.push_back(hub.engine_for({.subsystem = subsystem, .asset = rng::AssetKey{i}}));
    }
    return engines;
}

CorrelatedRandomWalk make_walk(std::size_t n, Dynamics dynamics) {
    const rng::RngHub hub{rng::Seed{1}};
    const auto size = static_cast<Index>(n);
    // A dense lower-triangular factor, so the product costs what it costs for a
    // real correlation matrix.
    Matrix chol = Matrix::Constant(size, size, 0.001);
    chol.diagonal().setConstant(0.01);
    return CorrelatedRandomWalk{Vector::Constant(size, 100.0),
                                Vector::Constant(size, 0.0),
                                chol.triangularView<Eigen::Lower>(),
                                engines_for(hub, rng::Subsystem::Price, n),
                                dynamics};
}

FactorModel make_factor_model(std::size_t n, Dynamics dynamics) {
    const rng::RngHub hub{rng::Seed{1}};
    const auto size = static_cast<Index>(n);
    const auto k = static_cast<Index>(factor_count);
    return FactorModel{Vector::Constant(size, 100.0),
                       Vector::Constant(size, 0.0),
                       Matrix::Constant(size, k, 0.5),
                       Matrix::Identity(k, k),
                       Vector::Constant(size, 0.01),
                       engines_for(hub, rng::Subsystem::Price, n),
                       engines_for(hub, rng::Subsystem::PriceFactor, factor_count),
                       dynamics};
}

} // namespace

int main() {
    ankerl::nanobench::Bench bench;
    bench.title("price step").warmup(100);

    {
        const rng::RngHub hub{rng::Seed{1}};
        rng::RngEngine engine =
            hub.engine_for({.subsystem = rng::Subsystem::Price, .asset = rng::AssetKey{0}});
        bench.run("standard_normal draw", [&] {
            ankerl::nanobench::doNotOptimizeAway(mms::math::dist::standard_normal(engine));
        });
    }

    for (const std::size_t n : {10U, 100U, 1000U}) {
        for (const Dynamics dynamics : {Dynamics::Arithmetic, Dynamics::Geometric}) {
            const std::string kind = dynamics == Dynamics::Arithmetic ? "arithmetic" : "geometric";

            CorrelatedRandomWalk walk = make_walk(n, dynamics);
            bench.run("random walk, " + kind + ", N=" + std::to_string(n), [&] {
                walk.advance(dt);
                ankerl::nanobench::doNotOptimizeAway(walk.prices());
            });

            FactorModel model = make_factor_model(n, dynamics);
            bench.run("factor model k=5, " + kind + ", N=" + std::to_string(n), [&] {
                model.advance(dt);
                ankerl::nanobench::doNotOptimizeAway(model.prices());
            });
        }
    }
    return 0;
}
