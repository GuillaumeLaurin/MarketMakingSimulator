/// \file
/// Tests for LOG_TRACE/DEBUG/INFO/WARN/ERROR: fmt substitution, runtime
/// gating through the current Logger's min_level, and the no-op-when-no-
/// current-Logger safety net. Compile-time elision itself (MMS_LOG_COMPILE_MIN_LEVEL
/// making a level vanish from a Release binary) is a property of what code
/// exists, not something a runtime assertion in one build config can show -
/// it's exercised by building this same module in both dev and release
/// presets (dev-mingw / release-mingw), not by an assertion here.

#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <logging/in_memory_sink.hpp>
#include <logging/logger.hpp>
#include <logging/macros.hpp>

using mms::logging::InMemorySink;
using mms::logging::Level;
using mms::logging::Logger;

namespace {

class CurrentGuard {
public:
    CurrentGuard() : previous_(mms::logging::current()) {}

    CurrentGuard(const CurrentGuard&) = delete;
    CurrentGuard& operator=(const CurrentGuard&) = delete;

    ~CurrentGuard() { mms::logging::set_current(previous_); }

private:
    Logger* previous_;
};

} // namespace

TEST_CASE("LOG_* macros are a silent no-op when no current Logger is set", "[logging][macros]") {
    const CurrentGuard guard;
    mms::logging::set_current(nullptr);

    LOG_INFO("this must not crash: {}", 42); // an uncaught exception here fails the test
}

TEST_CASE("LOG_INFO formats its arguments through fmt before reaching the sink",
          "[logging][macros]") {
    const CurrentGuard guard;
    Logger logger(Level::Trace);
    auto sink = std::make_unique<InMemorySink>();
    const InMemorySink* raw = sink.get();
    logger.add_sink(std::move(sink));
    mms::logging::set_current(&logger);

    LOG_INFO("order {} filled at {}", 7, 101.5);

    REQUIRE(raw->messages().size() == 1);
    CHECK(raw->messages()[0].first == Level::Info);
    CHECK(raw->messages()[0].second == "order 7 filled at 101.5");
}

TEST_CASE("LOG_* macros respect the current Logger's runtime floor", "[logging][macros]") {
    const CurrentGuard guard;
    Logger logger(Level::Warn);
    auto sink = std::make_unique<InMemorySink>();
    const InMemorySink* raw = sink.get();
    logger.add_sink(std::move(sink));
    mms::logging::set_current(&logger);

    LOG_INFO("dropped: below the floor");
    CHECK(raw->messages().empty());

    LOG_ERROR("kept: at the floor or above");
    REQUIRE(raw->messages().size() == 1);
    CHECK(raw->messages()[0].second == "kept: at the floor or above");
}

TEST_CASE("every LOG_* macro reaches the sink at its own level when enabled", "[logging][macros]") {
    const CurrentGuard guard;
    Logger logger(Level::Trace);
    auto sink = std::make_unique<InMemorySink>();
    const InMemorySink* raw = sink.get();
    logger.add_sink(std::move(sink));
    mms::logging::set_current(&logger);

    LOG_TRACE("t");
    LOG_DEBUG("d");
    LOG_INFO("i");
    LOG_WARN("w");
    LOG_ERROR("e");

    REQUIRE(raw->messages().size() == 5);
    CHECK(raw->messages()[0].first == Level::Trace);
    CHECK(raw->messages()[1].first == Level::Debug);
    CHECK(raw->messages()[2].first == Level::Info);
    CHECK(raw->messages()[3].first == Level::Warn);
    CHECK(raw->messages()[4].first == Level::Error);
}
