/// \file
/// Tests for mm/quoting_strategy.hpp, through a recording test strategy: the
/// non-virtual entry points must enforce the contract identically for every
/// strategy (invalid input never reaches the subclass), forward valid input
/// intact, and check the postcondition that the QuoteSet has one entry per
/// asset. Also the single Quantity to double conversion in make_market_view.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mm/quote.hpp>
#include <mm/quoting_strategy.hpp>
#include <mm/types.hpp>

#include <math/types.hpp>

namespace mm = mms::mm;
namespace math = mms::math;

namespace {

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

math::Vector vec(std::initializer_list<double> values) {
    math::Vector result(static_cast<Eigen::Index>(values.size()));
    Eigen::Index index = 0;
    for (const double value : values) {
        result(index++) = value;
    }
    return result;
}

/// Records what reaches the protected virtuals, and quotes 1 below and 1 above
/// each mid so a test can see that the view arrived intact.
class RecordingStrategy final : public mm::QuotingStrategy {
public:
    explicit RecordingStrategy(std::size_t assets, bool* destroyed = nullptr)
        : returned_assets(assets), assets_(assets), destroyed_(destroyed) {}

    ~RecordingStrategy() override {
        if (destroyed_ != nullptr) {
            *destroyed_ = true;
        }
    }

    RecordingStrategy(const RecordingStrategy&) = default;
    RecordingStrategy& operator=(const RecordingStrategy&) = default;
    RecordingStrategy(RecordingStrategy&&) noexcept = default;
    RecordingStrategy& operator=(RecordingStrategy&&) noexcept = default;

    [[nodiscard]] std::size_t asset_count() const noexcept override { return assets_; }

    int market_data_calls = 0;
    math::Vector last_mids;
    double last_dt = 0.0;

    mutable int quote_calls = 0;
    mutable std::optional<mm::MarketView> last_view;

    /// How many AssetQuotes do_quote returns (defaults to asset_count()).
    std::size_t returned_assets;

protected:
    void do_on_market_data(const math::Vector& mids, double dt) override {
        ++market_data_calls;
        last_mids = mids;
        last_dt = dt;
    }

    [[nodiscard]] mm::QuoteSet do_quote(const mm::MarketView& view) const override {
        ++quote_calls;
        last_view = view;
        mm::QuoteSet quotes(returned_assets);
        for (std::size_t asset = 0; asset < quotes.size() && std::cmp_less(asset, view.mids.size());
             ++asset) {
            const double mid = view.mids(static_cast<Eigen::Index>(asset));
            quotes[asset].bid = mm::Quote{.price = mid - 1.0, .quantity = 1};
            quotes[asset].ask = mm::Quote{.price = mid + 1.0, .quantity = 1};
        }
        return quotes;
    }

private:
    std::size_t assets_;
    bool* destroyed_;
};

} // namespace

TEST_CASE("QuotingStrategy is abstract with a virtual destructor", "[mm][quoting_strategy]") {
    STATIC_REQUIRE(std::is_abstract_v<mm::QuotingStrategy>);
    STATIC_REQUIRE(std::has_virtual_destructor_v<mm::QuotingStrategy>);
}

TEST_CASE("asset_count is whatever the concrete strategy reports", "[mm][quoting_strategy]") {
    const RecordingStrategy strategy(3);

    CHECK(strategy.asset_count() == 3);
}

TEST_CASE("on_market_data forwards valid mids and dt to the strategy", "[mm][quoting_strategy]") {
    RecordingStrategy strategy(2);

    strategy.on_market_data(vec({100.0, 50.0}), 0.25);

    CHECK(strategy.market_data_calls == 1);
    CHECK(strategy.last_mids == vec({100.0, 50.0}));
    CHECK(strategy.last_dt == 0.25);
}

TEST_CASE("on_market_data accepts a tiny positive dt", "[mm][quoting_strategy]") {
    RecordingStrategy strategy(1);

    strategy.on_market_data(vec({100.0}), 1.0e-12);

    CHECK(strategy.market_data_calls == 1);
}

TEST_CASE("on_market_data rejects a dt that is not finite and strictly positive, without "
          "reaching the strategy",
          "[mm][quoting_strategy]") {
    RecordingStrategy strategy(1);

    for (const double bad : {0.0, -0.1, quiet_nan, infinity, -infinity}) {
        CHECK_THROWS_AS(strategy.on_market_data(vec({100.0}), bad), std::invalid_argument);
    }
    CHECK(strategy.market_data_calls == 0);
}

TEST_CASE("on_market_data rejects mids that do not have one entry per asset",
          "[mm][quoting_strategy]") {
    RecordingStrategy strategy(2);

    CHECK_THROWS_AS(strategy.on_market_data(vec({100.0}), 1.0), std::invalid_argument);
    CHECK_THROWS_AS(strategy.on_market_data(vec({100.0, 50.0, 25.0}), 1.0), std::invalid_argument);
    CHECK(strategy.market_data_calls == 0);
}

TEST_CASE("on_market_data rejects non-finite mids", "[mm][quoting_strategy]") {
    RecordingStrategy strategy(2);

    CHECK_THROWS_AS(strategy.on_market_data(vec({100.0, quiet_nan}), 1.0), std::invalid_argument);
    CHECK_THROWS_AS(strategy.on_market_data(vec({infinity, 50.0}), 1.0), std::invalid_argument);
    CHECK(strategy.market_data_calls == 0);
}

TEST_CASE("quote passes the view intact to the strategy and returns its quotes",
          "[mm][quoting_strategy]") {
    const RecordingStrategy strategy(2);
    const mm::MarketView view{
        .mids = vec({100.0, 50.0}), .inventory = vec({3.0, -4.0}), .time = 7.5};

    const mm::QuoteSet quotes = strategy.quote(view);

    REQUIRE(strategy.last_view.has_value());
    CHECK(strategy.last_view->mids == view.mids);
    CHECK(strategy.last_view->inventory == view.inventory);
    CHECK(strategy.last_view->time == 7.5);

    REQUIRE(quotes.size() == 2);
    CHECK(quotes[0].bid == mm::Quote{.price = 99.0, .quantity = 1});
    CHECK(quotes[0].ask == mm::Quote{.price = 101.0, .quantity = 1});
    CHECK(quotes[1].bid == mm::Quote{.price = 49.0, .quantity = 1});
}

TEST_CASE("quote rejects a view whose vectors do not have one entry per asset",
          "[mm][quoting_strategy]") {
    const RecordingStrategy strategy(2);

    CHECK_THROWS_AS(strategy.quote(mm::MarketView{
                        .mids = vec({100.0}), .inventory = vec({0.0, 0.0}), .time = 0.0}),
                    std::invalid_argument);
    CHECK_THROWS_AS(strategy.quote(mm::MarketView{
                        .mids = vec({100.0, 50.0}), .inventory = vec({0.0}), .time = 0.0}),
                    std::invalid_argument);
    CHECK(strategy.quote_calls == 0);
}

TEST_CASE("quote rejects a view holding a non-finite value", "[mm][quoting_strategy]") {
    const RecordingStrategy strategy(1);

    CHECK_THROWS_AS(strategy.quote(mm::MarketView{
                        .mids = vec({quiet_nan}), .inventory = vec({0.0}), .time = 0.0}),
                    std::invalid_argument);
    CHECK_THROWS_AS(strategy.quote(mm::MarketView{
                        .mids = vec({100.0}), .inventory = vec({infinity}), .time = 0.0}),
                    std::invalid_argument);
    CHECK_THROWS_AS(strategy.quote(mm::MarketView{
                        .mids = vec({100.0}), .inventory = vec({0.0}), .time = quiet_nan}),
                    std::invalid_argument);
    CHECK_THROWS_AS(strategy.quote(mm::MarketView{
                        .mids = vec({100.0}), .inventory = vec({0.0}), .time = infinity}),
                    std::invalid_argument);
    CHECK(strategy.quote_calls == 0);
}

TEST_CASE("quote throws std::logic_error, not invalid_argument, when the strategy returns the "
          "wrong number of assets",
          "[mm][quoting_strategy]") {
    RecordingStrategy strategy(2);
    const mm::MarketView view{
        .mids = vec({100.0, 50.0}), .inventory = vec({0.0, 0.0}), .time = 0.0};

    for (const std::size_t wrong : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {
        strategy.returned_assets = wrong;

        CHECK_THROWS_AS(strategy.quote(view), std::logic_error);

        bool blamed_the_caller = false;
        try {
            static_cast<void>(strategy.quote(view));
        } catch (const std::logic_error& error) {
            blamed_the_caller = dynamic_cast<const std::invalid_argument*>(&error) != nullptr;
        }
        CHECK_FALSE(blamed_the_caller);
    }
}

TEST_CASE("A strategy with no assets accepts empty input and returns no quotes",
          "[mm][quoting_strategy]") {
    RecordingStrategy strategy(0);

    CHECK_NOTHROW(strategy.on_market_data(math::Vector(0), 1.0));
    const mm::QuoteSet quotes = strategy.quote(
        mm::MarketView{.mids = math::Vector(0), .inventory = math::Vector(0), .time = 0.0});

    CHECK(quotes.empty());
}

TEST_CASE("A strategy is used, and destroyed, through the base class", "[mm][quoting_strategy]") {
    bool destroyed = false;
    {
        const std::unique_ptr<mm::QuotingStrategy> strategy =
            std::make_unique<RecordingStrategy>(1, &destroyed);

        strategy->on_market_data(vec({100.0}), 1.0);
        const mm::QuoteSet quotes = strategy->quote(
            mm::MarketView{.mids = vec({100.0}), .inventory = vec({0.0}), .time = 0.0});

        CHECK(quotes.size() == 1);
        CHECK_FALSE(destroyed);
    }
    CHECK(destroyed); // the derived destructor ran through the virtual base destructor
}

TEST_CASE("inventory_vector converts every Quantity to a double, keeping order and sign",
          "[mm][quoting_strategy]") {
    const math::Vector inventory = mm::inventory_vector({10, -4, 0, 123456789});

    REQUIRE(inventory.size() == 4);
    CHECK(inventory(0) == 10.0);
    CHECK(inventory(1) == -4.0);
    CHECK(inventory(2) == 0.0);
    CHECK(inventory(3) == 123456789.0);
}

TEST_CASE("inventory_vector of nothing is an empty vector", "[mm][quoting_strategy]") {
    CHECK(mm::inventory_vector({}).size() == 0);
}

TEST_CASE("inventory_vector is exact up to 2^53", "[mm][quoting_strategy]") {
    const mm::Quantity big = mm::Quantity{1} << 53;

    const math::Vector inventory = mm::inventory_vector({big, -big});

    CHECK(inventory(0) == 9007199254740992.0);
    CHECK(inventory(1) == -9007199254740992.0);
}

TEST_CASE("make_market_view converts the inventory and keeps mids and time",
          "[mm][quoting_strategy]") {
    const mm::MarketView view = mm::make_market_view(vec({100.0, 50.0}), {7, -2}, 12.5);

    CHECK(view.mids == vec({100.0, 50.0}));
    CHECK(view.inventory == vec({7.0, -2.0}));
    CHECK(view.time == 12.5);
}

TEST_CASE("A MarketView owns its data and does not follow later changes to its source",
          "[mm][quoting_strategy]") {
    math::Vector mids = vec({100.0, 50.0});
    const mm::MarketView view = mm::make_market_view(mids, {1, 2}, 0.0);

    mids(0) = 999.0;

    CHECK(view.mids(0) == 100.0);
}
