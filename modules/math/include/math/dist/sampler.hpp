#ifndef MMS_MATH_DIST_SAMPLER_HPP
#define MMS_MATH_DIST_SAMPLER_HPP

/// \file
/// Batch helpers over any Sampler. generate() fills a span; discard() skips n
/// draws in O(1) for a fixed-consumption sampler by jumping the engine. Free
/// templates, no base class, no CRTP. AnySampler<T> (the type-erased config-seam
/// wrapper) will land here too, once the mms config layer needs it.

#include <cstddef>
#include <span>

#include <math/dist/primitives.hpp>
#include <math/dist/traits.hpp>

namespace mms::math::dist {

/// A Uint64Source whose position can be advanced in O(1).
template <class G>
concept JumpableUint64Source = Uint64Source<G> && requires(G& g, unsigned long long z) {
    g.discard(z);
};

/// Draw out.size() variates into out.
template <Sampler S, Uint64Source G>
void generate(const S& sampler, G& g, std::span<typename S::result_type> out) {
    for (auto& x : out) {
        x = sampler(g);
    }
}

/// Skip n draws. Defined only for fixed-consumption samplers: advances the 
/// engine by exaclty n * words_per_sample words, the distance n calls to
/// operator() would cover.
template <Sampler S, JumpableUint64Source G>
    requires(S::words_per_sample >= 0)
void discard(const S& /*sampler*/, G& g, std::size_t n) {
    g.discard(static_cast<unsigned long long>(n) *
              static_cast<unsigned long long>(S::words_per_sample));
}

} // namespace mms::math::dist

#endif // MMS_MATH_DIST_SAMPLER_HPP