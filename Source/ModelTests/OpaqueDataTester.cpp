#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <doctest/doctest.h>
#include "Rellu/Model/OpaqueData.hpp"

using namespace Rellu;

TEST_SUITE("OpaqueData") {
  TEST_CASE("empty") {
    auto opaque = OpaqueData();
    REQUIRE(opaque.is_empty());
    REQUIRE(opaque.get_bytes().empty());
    REQUIRE(OpaqueData(std::string()).is_empty());
    REQUIRE(opaque == OpaqueData(std::string()));
    REQUIRE(sizeof(OpaqueData) == sizeof(void*));
  }

  TEST_CASE("sharing") {
    auto opaque = OpaqueData(std::string("a\0b", 3));
    REQUIRE(opaque.get_bytes() == std::string_view("a\0b", 3));
    auto copy = opaque;
    REQUIRE(copy.get_bytes().data() == opaque.get_bytes().data());
    auto moved = std::move(copy);
    REQUIRE(moved == opaque);
    copy = moved;
    copy = OpaqueData();
    REQUIRE(copy.is_empty());
    REQUIRE(moved.get_bytes() == std::string_view("a\0b", 3));
  }

  TEST_CASE("equality") {
    REQUIRE(OpaqueData(std::string("x")) == OpaqueData(std::string("x")));
    REQUIRE(OpaqueData(std::string("x")) != OpaqueData(std::string("y")));
    REQUIRE(OpaqueData(std::string("x")) != OpaqueData());
  }
}
