#ifndef MMS_SIMULATION_ACCOUNT_RULES_HPP
#define MMS_SIMULATION_ACCOUNT_RULES_HPP

/// \file
/// The rules of the exchange that apply to every account alike. One set per
/// simulation, not per agent.

namespace mms::simulation {

struct AccountRules {
    /// Share of a short position's value held back as collateral, valued at the
    /// last mid. 1.0 means fully collateralised: a short locks as much cash as it
    /// brought in, so it gives no leverage. Below 1.0 gives leverage, and nothing in
    /// the simulation liquidates an account that loses too much, so lower it
    /// knowingly. Finite and not negative.
    double short_collateral_rate = 1.0;

    /// A market order has no price, so its cost is estimated at the last mid plus
    /// this fraction, to leave room for the book moving as it is taken. 0.05 is 5 %.
    /// Finite and not negative.
    double market_order_buffer = 0.05;
};

} // namespace mms::simulation

#endif // MMS_SIMULATION_ACCOUNT_RULES_HPP