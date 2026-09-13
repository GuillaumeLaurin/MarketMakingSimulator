/// \file
/// Tests for Logger's own responsibility: the runtime level floor, dispatch
/// to every attached sink, and the thread-local current()/set_current()
/// access point. InMemorySink is used here only as an already-independently-
/// tested observation probe for Logger's dispatch logic, not as a stand-in
/// for testing a sink's own behavior (see in_memory_sink.cpp, console_sink.cpp
/// and file_sink.cpp for that).

#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <logging/in_memory_sink.hpp>
#include <logging/logger.hpp>

using mms::logging::InMemorySink;
using mms::logging::Level;
using mms::logging::Logger;

namespace {

/// Saves and restores the thread-local current() Logger around a test case,
/// so tests never leak state into each other regardless of run order.
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

TEST_CASE("Logger defaults to Info and enabled() reflects the floor", "[logging][logger]") {
    const Logger logger;

    CHECK(logger.min_level() == Level::Info);
    CHECK_FALSE(logger.enabled(Level::Trace));
    CHECK_FALSE(logger.enabled(Level::Debug));
    CHECK(logger.enabled(Level::Info));
    CHECK(logger.enabled(Level::Warn));
    CHECK(logger.enabled(Level::Error));
}

TEST_CASE("Logger::set_min_level moves the floor", "[logging][logger]") {
    Logger logger;
    logger.set_min_level(Level::Error);

    CHECK(logger.min_level() == Level::Error);
    CHECK_FALSE(logger.enabled(Level::Warn));
    CHECK(logger.enabled(Level::Error));
}

TEST_CASE("Logger::log dispatches to every attached sink when the level is enabled",
          "[logging][logger]") {
    Logger logger(Level::Trace);

    auto sink_a = std::make_unique<InMemorySink>();
    auto sink_b = std::make_unique<InMemorySink>();
    const InMemorySink* raw_a = sink_a.get();
    const InMemorySink* raw_b = sink_b.get();
    logger.add_sink(std::move(sink_a));
    logger.add_sink(std::move(sink_b));

    logger.log(Level::Warn, "reached both sinks");

    REQUIRE(raw_a->messages().size() == 1);
    CHECK(raw_a->messages()[0].first == Level::Warn);
    CHECK(raw_a->messages()[0].second == "reached both sinks");
    REQUIRE(raw_b->messages().size() == 1);
    CHECK(raw_b->messages()[0].second == "reached both sinks");
}

TEST_CASE("Logger::log drops a message below the runtime floor, no sink is touched",
          "[logging][logger]") {
    Logger logger(Level::Warn);

    auto sink = std::make_unique<InMemorySink>();
    const InMemorySink* raw = sink.get();
    logger.add_sink(std::move(sink));

    logger.log(Level::Info, "should never arrive");

    CHECK(raw->messages().empty());
}

TEST_CASE("current() is nullptr until set_current is called on this thread", "[logging][logger]") {
    const CurrentGuard guard;
    mms::logging::set_current(nullptr);

    CHECK(mms::logging::current() == nullptr);
}

TEST_CASE("set_current/current round-trip", "[logging][logger]") {
    const CurrentGuard guard;
    Logger logger;

    mms::logging::set_current(&logger);
    CHECK(mms::logging::current() == &logger);

    mms::logging::set_current(nullptr);
    CHECK(mms::logging::current() == nullptr);
}
