/// \file
/// Tests for InMemorySink, on its own terms (not as a stand-in for testing
/// ConsoleSink or FileSink - it is one Sink implementation among others,
/// with its own contract: accumulate what it's given, verbatim, in order).

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <logging/in_memory_sink.hpp>

using mms::logging::InMemorySink;
using mms::logging::Level;

TEST_CASE("InMemorySink starts empty", "[logging][in_memory_sink]") {
    const InMemorySink sink;
    CHECK(sink.messages().empty());
}

TEST_CASE("InMemorySink accumulates messages verbatim, in order", "[logging][in_memory_sink]") {
    InMemorySink sink;
    sink.write(Level::Info, "first");
    sink.write(Level::Error, "second");

    REQUIRE(sink.messages().size() == 2);
    CHECK(sink.messages()[0].first == Level::Info);
    CHECK(sink.messages()[0].second == "first");
    CHECK(sink.messages()[1].first == Level::Error);
    CHECK(sink.messages()[1].second == "second");
}

TEST_CASE("InMemorySink::write copies the message (a temporary string_view source is safe)",
          "[logging][in_memory_sink]") {
    InMemorySink sink;
    {
        const std::string temp = "built on the stack";
        sink.write(Level::Debug, temp);
    } // temp is destroyed here

    REQUIRE(sink.messages().size() == 1);
    CHECK(sink.messages()[0].second == "built on the stack");
}

TEST_CASE("InMemorySink::clear empties the buffer", "[logging][in_memory_sink]") {
    InMemorySink sink;
    sink.write(Level::Warn, "will be cleared");
    REQUIRE_FALSE(sink.messages().empty());

    sink.clear();
    CHECK(sink.messages().empty());
}
