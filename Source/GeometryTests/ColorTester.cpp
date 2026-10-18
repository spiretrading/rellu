#include <sstream>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Color.hpp"

using namespace Rellu;

TEST_SUITE("Color") {
  TEST_CASE("make_color") {
    REQUIRE(make_color(255, 0, 51) == Color(1, 0, 0.2, 1));
    REQUIRE(make_color(0, 255, 0, 0.5) == Color(0, 1, 0, 0.5));
    REQUIRE(make_color(102, 102, 102).m_red == doctest::Approx(0.4));
  }

  TEST_CASE("make_argb_color") {
    REQUIRE(make_argb_color(4278190080) == Color(0, 0, 0, 1));
    REQUIRE(make_argb_color(0xFFFFFFFF) == Color(1, 1, 1, 1));
    REQUIRE(make_argb_color(0x00FF0000) == Color(1, 0, 0, 0));
    REQUIRE(make_argb_color(0x3300FF33) == make_color(0, 255, 51, 0.2));
  }

  TEST_CASE("equality") {
    REQUIRE(make_color(10, 20, 30) == make_color(10, 20, 30, 1));
    REQUIRE(make_color(10, 20, 30) != make_color(10, 20, 31));
    REQUIRE(make_color(10, 20, 30) != make_color(10, 20, 30, 0.5));
  }

  TEST_CASE("stream") {
    auto stream = std::ostringstream();
    stream << Color(1, 0.5, 0, 0.25);
    REQUIRE(stream.str() == "(1, 0.5, 0, 0.25)");
  }
}
