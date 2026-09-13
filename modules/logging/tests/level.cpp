/// \file
/// Tests for Level and to_string(Level).

#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <logging/level.hpp>

using mms::logging::Level;
using mms::logging::to_string;

TEST_CASE("to_string maps every Level to its name", "[logging][level]") {
    CHECK(std::string_view(to_string(Level::Trace)) == "TRACE");
    CHECK(std::string_view(to_string(Level::Debug)) == "DEBUG");
    CHECK(std::string_view(to_string(Level::Info)) == "INFO");
    CHECK(std::string_view(to_string(Level::Warn)) == "WARN");
    CHECK(std::string_view(to_string(Level::Error)) == "ERROR");
}

TEST_CASE("Level orders Trace < Debug < Info < Warn < Error", "[logging][level]") {
    CHECK(Level::Trace < Level::Debug);
    CHECK(Level::Debug < Level::Info);
    CHECK(Level::Info < Level::Warn);
    CHECK(Level::Warn < Level::Error);
}
