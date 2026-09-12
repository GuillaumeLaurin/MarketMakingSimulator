#ifndef MMS_MATH_PROCESS_PRICE_PROCESS_HPP
#define MMS_MATH_PROCESS_PRICE_PROCESS_HPP

/// \file
/// PriceProcess: the abstract interface every concrete price-path model
/// implements. A pure virtual base (not a concept / type erasure): a process
/// is a heavy, uniquely-owned object picked from config at construction and
/// called once per tick, where a virtual call is negligible against the rest
/// of a tick's work. `simulation` holds a `std::unique_ptr<PriceProcess>`.

#include <stdexcept>

#include <math/types.hpp>

namespace mms::math::process {

class PriceProcess {
public:
    virtual ~PriceProcess() = default;

    /// Advances by dt. Throws std::invalid_argument if dt is not finite and
    /// >= 0 - a contract every PriceProcess honors identically, regardless of
    /// implementation (Liskov: no subclass may relax or strengthen this).
    void advance(double dt) {
        if (!(dt >= 0.0)) {
            throw std::invalid_argument("PriceProcess::advance: dt must be finite and >= 0");
        }
        do_advance(dt);
    }

    /// Current prices, one per asset, in the order fixed at construction.
    [[nodiscard]] virtual const Vector& prices() const noexcept = 0;

    /// Restore prices and RNG engine state to what they were at construction.
    virtual void reset() = 0;

    [[nodiscard]] virtual Index asset_count() const noexcept = 0;

protected:
    virtual void do_advance(double dt) = 0;
};

} // namespace mms::math::process

#endif // MMS_MATH_PROCESS_PRICE_PROCESS_HPP