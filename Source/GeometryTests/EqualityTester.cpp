#include <limits>
#include <map>
#include <optional>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Size.hpp"

using namespace Rellu;

namespace {
  constexpr auto NAN_VALUE = std::numeric_limits<double>::quiet_NaN();
}

TEST_SUITE("Equality") {
  TEST_CASE("numbers") {
    REQUIRE(equals(1.5, 1.5));
    REQUIRE(equals(-0.0, 0.0));
    REQUIRE(equals(NAN_VALUE, -NAN_VALUE));
    REQUIRE(!equals(NAN_VALUE, 0));
    REQUIRE(!equals(1, 2));
    REQUIRE(equals(std::optional<double>(NAN_VALUE), NAN_VALUE));
    REQUIRE(equals(std::optional<double>(), std::optional<double>()));
    REQUIRE(!equals(std::optional<double>(), NAN_VALUE));
    REQUIRE(equals(std::vector<double>({1, NAN_VALUE}),
      std::vector<double>({1, NAN_VALUE})));
    REQUIRE(!equals(std::vector<double>({1, NAN_VALUE}),
      std::vector<double>({1})));
    REQUIRE(equals(std::map<int, double>({{1, NAN_VALUE}}),
      std::map<int, double>({{1, NAN_VALUE}})));
    REQUIRE(!equals(std::map<int, double>({{1, NAN_VALUE}}),
      std::map<int, double>({{2, NAN_VALUE}})));
  }

  TEST_CASE("values") {
    REQUIRE(Point(NAN_VALUE, 1) == Point(NAN_VALUE, 1));
    REQUIRE(Point(NAN_VALUE, 1) != Point(NAN_VALUE, 2));
    REQUIRE(Size(NAN_VALUE, 0) == Size(NAN_VALUE, 0));
    REQUIRE(Rect(0, NAN_VALUE, 1, 1) == Rect(0, NAN_VALUE, 1, 1));
    REQUIRE(Matrix(1, 0, 0, NAN_VALUE, 0, 0) ==
      Matrix(1, 0, 0, NAN_VALUE, 0, 0));
    REQUIRE(Matrix(1, 0, 0, NAN_VALUE, 0, 0) != Matrix());
    REQUIRE(Color(NAN_VALUE, 0, 0, 1) == Color(NAN_VALUE, 0, 0, 1));
  }
}
