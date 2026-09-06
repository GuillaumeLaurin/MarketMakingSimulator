#ifndef MMS_MATH_DIST_SAMPLER_HPP
#define MMS_MATH_DIST_SAMPLER_HPP

/// \file
/// Batch helpers over any Sampler. generate() fills a span; discard() skips n
/// draws in O(1) for a fixed-consumption sampler by jumping the engine. Free
/// templates, no base class, no CRTP. AnySampler<T> (the type-erased config-seam
/// wrapper) will land here too, once the mms config layer needs it.

#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#include <math/dist/primitives.hpp>
#include <math/dist/traits.hpp>
#include <math/rng/engine.hpp>
#include <math/rng/stream.hpp>

namespace mms::math::dist {

/// A Uint64Source whose position can be advanced in O(1).
template<class G>
concept JumpableUint64Source =
    Uint64Source<G> && requires(G& g, unsigned long long z) { g.discard(z); };

/// Type-erased sampler for the mms config seam: a value type holding any Sampler
/// whose result_type is T, chosen at run time. Small-buffer storage (no heap per
/// draw), one indirect call per draw. Not itself a Sampler (words_per_sample is
/// a run-time method, not a constant), so it never wraps itself.
template<class T> class AnySampler {
public:
    using result_type = T;

    /// Bytes of inline storage. Sized for the largest built-in sampler
    /// (Discrete: two std::vector members). A sampler that does not fit trips a
    /// static_assert in the constructor.
    static constexpr std::size_t buffer_capacity = 64;

    template<Sampler S> explicit AnySampler(S sampler) : vtable_(&vtable_for<S>()) {
        static_assert(sizeof(S) <= buffer_capacity, "sampler exceeds AnySampler buffer_capacity");
        static_assert(alignof(S) <= alignof(std::max_align_t),
                      "sampler over-aligned for AnySampler");
        static_assert(std::same_as<typename S::result_type, T>,
                      "sampler result_type must equal AnySampler<T>");
        static_assert(std::is_nothrow_move_constructible_v<S>, "sampler must be nothrow-movable");
        ::new (raw()) S(std::move(sampler));
    }

    AnySampler(const AnySampler& other) : vtable_(other.vtable_) {
        vtable_->copy(other.raw(), raw());
    }

    AnySampler(AnySampler&& other) noexcept : vtable_(other.vtable_) {
        vtable_->move(other.raw(), raw());
    }

    AnySampler& operator=(const AnySampler& other) {
        if (this != &other) {
            vtable_->destroy(raw());
            vtable_ = other.vtable_;
            vtable_->copy(other.raw(), raw());
        }
        return *this;
    }

    AnySampler& operator=(AnySampler&& other) noexcept {
        if (this != &other) {
            vtable_->destroy(raw());
            vtable_ = other.vtable_;
            vtable_->move(other.raw(), raw());
        }
        return *this;
    }

    ~AnySampler() { vtable_->destroy(raw()); }

    [[nodiscard]] T operator()(rng::RngEngine& g) const { return vtable_->call(raw(), g); }

    /// Engine-word cost of the wrapped sampler; -1 if variable.
    [[nodiscard]] int words_per_sample() const noexcept { return vtable_->words; }

    /// Skip n draws. O(1) engine jump for a fixed-consumption sampler, otherwise
    /// an n-call replay.
    void discard(rng::RngEngine& g, std::size_t n) const { vtable_->discard(raw(), g, n); }

private:
    [[nodiscard]] void* raw() noexcept { return storage_.data(); }

    [[nodiscard]] const void* raw() const noexcept { return storage_.data(); }

    struct VTable {
        T (*call)(const void*, rng::RngEngine&);
        void (*copy)(const void*, void*);
        void (*move)(void*, void*) noexcept;
        void (*destroy)(void*) noexcept;
        void (*discard)(const void*, rng::RngEngine&, std::size_t);
        int words;
    };

    template<class S> static const VTable& vtable_for() noexcept {
        static constexpr VTable table{
            [](const void* p, rng::RngEngine& g) -> T { return (*static_cast<const S*>(p))(g); },
            [](const void* src, void* dst) { ::new (dst) S(*static_cast<const S*>(src)); },
            [](void* src, void* dst) noexcept { ::new (dst) S(std::move(*static_cast<S*>(src))); },
            [](void* p) noexcept { std::destroy_at(static_cast<S*>(p)); },
            []([[maybe_unused]] const void* p, rng::RngEngine& g, std::size_t n) {
                if constexpr (S::words_per_sample >= 0) {
                    g.discard(static_cast<unsigned long long>(n) *
                              static_cast<unsigned long long>(S::words_per_sample));
                } else {
                    const auto& s = *static_cast<const S*>(p);
                    for (std::size_t i = 0; i < n; ++i) {
                        (void)s(g);
                    }
                }
            },
            S::words_per_sample,
        };
        return table;
    }

    alignas(std::max_align_t) std::array<std::byte, buffer_capacity> storage_{};
    const VTable* vtable_;
};

/// Draw out.size() variates into out.
template<Sampler S, Uint64Source G>
void generate(const S& sampler, G& g, std::span<typename S::result_type> out) {
    for (auto& x : out) {
        x = sampler(g);
    }
}

/// generate() for a type-erased sampler (tied to the concrete engine).
template<class T> void generate(const AnySampler<T>& sampler, rng::RngEngine& g, std::span<T> out) {
    for (T& x : out) {
        x = sampler(g);
    }
}

/// Skip n draws. Defined only for fixed-consumption samplers: advances the
/// engine by exaclty n * words_per_sample words, the distance n calls to
/// operator() would cover.
template<Sampler S, JumpableUint64Source G>
    requires(S::words_per_sample >= 0)
void discard(const S& /*sampler*/, G& g, std::size_t n) {
    g.discard(static_cast<unsigned long long>(n) *
              static_cast<unsigned long long>(S::words_per_sample));
}

} // namespace mms::math::dist

#endif // MMS_MATH_DIST_SAMPLER_HPP