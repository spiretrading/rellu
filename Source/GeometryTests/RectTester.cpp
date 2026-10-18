#include <sstream>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Rect.hpp"

using namespace Rellu;

TEST_SUITE("Rect") {
  TEST_CASE("edges") {
    auto rect = Rect(10, 20, 30, 40);
    REQUIRE(rect.get_left() == 10);
    REQUIRE(rect.get_top() == 20);
    REQUIRE(rect.get_right() == 40);
    REQUIRE(rect.get_bottom() == 60);
    REQUIRE(rect.get_top_left() == Point(10, 20));
    REQUIRE(rect.get_bottom_right() == Point(40, 60));
    REQUIRE(rect.get_center() == Point(25, 40));
    REQUIRE(rect.get_size() == Size(30, 40));
  }

  TEST_CASE("is_empty") {
    REQUIRE(!Rect(0, 0, 1, 1).is_empty());
    REQUIRE(Rect(0, 0, 0, 1).is_empty());
    REQUIRE(Rect(5, 5, 1, -1).is_empty());
    REQUIRE(Rect().is_empty());
  }

  TEST_CASE("contains_point") {
    auto rect = Rect(0, 0, 10, 10);
    REQUIRE(rect.contains(Point(5, 5)));
    REQUIRE(rect.contains(Point(0, 0)));
    REQUIRE(!rect.contains(Point(10, 5)));
    REQUIRE(!rect.contains(Point(5, 10)));
    REQUIRE(!rect.contains(Point(-1, 5)));
    REQUIRE(!Rect(0, 0, 0, 10).contains(Point(0, 5)));
  }

  TEST_CASE("contains_rect") {
    auto rect = Rect(0, 0, 10, 10);
    REQUIRE(rect.contains(Rect(2, 2, 3, 3)));
    REQUIRE(rect.contains(rect));
    REQUIRE(!rect.contains(Rect(5, 5, 6, 1)));
    REQUIRE(!rect.contains(Rect(2, 2, 0, 0)));
    REQUIRE(!Rect().contains(Rect()));
  }

  TEST_CASE("intersects") {
    auto rect = Rect(0, 0, 10, 10);
    REQUIRE(rect.intersects(Rect(5, 5, 10, 10)));
    REQUIRE(rect.intersects(Rect(2, 2, 1, 1)));
    REQUIRE(!rect.intersects(Rect(10, 0, 5, 5)));
    REQUIRE(!rect.intersects(Rect(20, 20, 5, 5)));
    REQUIRE(!rect.intersects(Rect(5, 5, 0, 0)));
  }

  TEST_CASE("intersect") {
    REQUIRE(intersect(Rect(0, 0, 10, 10), Rect(5, -5, 10, 10)) ==
      Rect(5, 0, 5, 5));
    REQUIRE(intersect(Rect(0, 0, 10, 10), Rect(2, 3, 4, 5)) ==
      Rect(2, 3, 4, 5));
    REQUIRE(intersect(Rect(0, 0, 10, 10), Rect(10, 10, 5, 5)) == Rect());
  }

  TEST_CASE("unite") {
    REQUIRE(unite(Rect(0, 0, 10, 10), Rect(5, -5, 10, 10)) ==
      Rect(0, -5, 15, 15));
    REQUIRE(unite(Rect(0, 0, 10, 10), Rect(50, 50, 0, 0)) ==
      Rect(0, 0, 10, 10));
    REQUIRE(unite(Rect(50, 50, 0, 0), Rect(1, 2, 3, 4)) == Rect(1, 2, 3, 4));
  }

  TEST_CASE("inflate") {
    REQUIRE(inflate(Rect(10, 10, 20, 20), 5) == Rect(5, 5, 30, 30));
    REQUIRE(inflate(Rect(10, 10, 20, 20), 2, -3) == Rect(8, 13, 24, 14));
    REQUIRE(inflate(Rect(10, 10, 4, 4), -3).is_empty());
  }

  TEST_CASE("stream") {
    auto stream = std::ostringstream();
    stream << Rect(1, 2, 3.5, 4);
    REQUIRE(stream.str() == "(1, 2, 3.5, 4)");
  }
}
