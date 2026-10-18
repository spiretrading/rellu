#include <limits>
#include <sstream>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Size.hpp"

using namespace Rellu;

TEST_SUITE("Size") {
  TEST_CASE("is_empty") {
    REQUIRE(!Size(1, 1).is_empty());
    REQUIRE(Size(0, 1).is_empty());
    REQUIRE(Size(1, 0).is_empty());
    REQUIRE(Size(-1, 5).is_empty());
    REQUIRE(Size(std::numeric_limits<double>::quiet_NaN(), 1).is_empty());
  }

  TEST_CASE("stream") {
    auto stream = std::ostringstream();
    stream << Size(3, 4.5);
    REQUIRE(stream.str() == "(3, 4.5)");
  }
}
