#include <sstream>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Point.hpp"

using namespace Rellu;

TEST_SUITE("Point") {
  TEST_CASE("arithmetic") {
    auto left = Point(1, 2);
    auto right = Point(3, -5);
    REQUIRE(left + right == Point(4, -3));
    REQUIRE(left - right == Point(-2, 7));
    REQUIRE(-left == Point(-1, -2));
    REQUIRE(left * 3 == Point(3, 6));
    REQUIRE(3 * left == Point(3, 6));
    REQUIRE(right / 2 == Point(1.5, -2.5));
  }

  TEST_CASE("dot") {
    REQUIRE(dot(Point(1, 2), Point(3, 4)) == 11);
    REQUIRE(dot(Point(1, 0), Point(0, 1)) == 0);
  }

  TEST_CASE("cross") {
    REQUIRE(cross(Point(1, 0), Point(0, 1)) == 1);
    REQUIRE(cross(Point(0, 1), Point(1, 0)) == -1);
    REQUIRE(cross(Point(2, 4), Point(1, 2)) == 0);
  }

  TEST_CASE("length") {
    REQUIRE(length(Point(3, 4)) == 5);
    REQUIRE(length(Point(0, 0)) == 0);
  }

  TEST_CASE("distance") {
    REQUIRE(distance(Point(1, 1), Point(4, 5)) == 5);
  }

  TEST_CASE("interpolate") {
    auto start = Point(0, 10);
    auto end = Point(10, 20);
    REQUIRE(interpolate(start, end, 0) == start);
    REQUIRE(interpolate(start, end, 1) == end);
    REQUIRE(interpolate(start, end, 0.25) == Point(2.5, 12.5));
  }

  TEST_CASE("stream") {
    auto stream = std::ostringstream();
    stream << Point(1.5, -2);
    REQUIRE(stream.str() == "(1.5, -2)");
  }
}
