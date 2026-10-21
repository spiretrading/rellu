#include <numbers>
#include <sstream>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Matrix.hpp"

using namespace Rellu;

namespace {
  bool is_near(Point left, Point right) {
    return distance(left, right) < 1e-9;
  }

  bool is_near(const Matrix& left, const Matrix& right) {
    return is_near(Point(left.m_a, left.m_b), Point(right.m_a, right.m_b)) &&
      is_near(Point(left.m_c, left.m_d), Point(right.m_c, right.m_d)) &&
      is_near(Point(left.m_tx, left.m_ty), Point(right.m_tx, right.m_ty));
  }
}

TEST_SUITE("Matrix") {
  TEST_CASE("identity") {
    auto matrix = Matrix();
    REQUIRE(matrix.is_identity());
    REQUIRE(matrix == Matrix(1, 0, 0, 1, 0, 0));
    REQUIRE(matrix.map(Point(3, -4)) == Point(3, -4));
    REQUIRE(!make_translation(1, 0).is_identity());
  }

  TEST_CASE("map") {
    auto matrix = Matrix(2, 3, 5, 7, 11, 13);
    REQUIRE(matrix.map(Point(1, 0)) == Point(13, 16));
    REQUIRE(matrix.map(Point(0, 1)) == Point(16, 20));
    REQUIRE(matrix.map(Point(2, 3)) == Point(30, 40));
  }

  TEST_CASE("translation") {
    REQUIRE(make_translation(5, -2).map(Point(1, 1)) == Point(6, -1));
  }

  TEST_CASE("scale") {
    REQUIRE(make_scale(2, -3).map(Point(4, 5)) == Point(8, -15));
  }

  TEST_CASE("rotation") {
    auto matrix = make_rotation(std::numbers::pi / 2);
    REQUIRE(is_near(matrix.map(Point(1, 0)), Point(0, 1)));
    REQUIRE(is_near(matrix.map(Point(0, 1)), Point(-1, 0)));
  }

  TEST_CASE("rotation_about_point") {
    auto matrix = make_rotation(std::numbers::pi, Point(10, 10));
    REQUIRE(is_near(matrix.map(Point(10, 10)), Point(10, 10)));
    REQUIRE(is_near(matrix.map(Point(12, 11)), Point(8, 9)));
  }

  TEST_CASE("get_rotation") {
    REQUIRE(get_rotation(Matrix()) == 0);
    REQUIRE(get_rotation(make_translation(5, 6)) == 0);
    REQUIRE(get_rotation(Matrix(1, -0.0, 0, 1, 0, 0)) == 0);
    REQUIRE(get_rotation(Matrix(-1, 0, 0, -1, 499, 409)) == 180);
    REQUIRE(get_rotation(Matrix(-1, -0.0, 0, -1, 0, 0)) == 180);
    REQUIRE(get_rotation(make_rotation(std::numbers::pi / 2)) == 90);
    REQUIRE(get_rotation(make_rotation(-std::numbers::pi / 2)) == -90);
    REQUIRE(get_rotation(make_rotation(-std::numbers::pi)) == 180);
    REQUIRE(get_rotation(make_rotation(std::numbers::pi / 4)) == 45);
    REQUIRE(get_rotation(make_rotation(0.5)) ==
      doctest::Approx(0.5 * 180 / std::numbers::pi));
  }

  TEST_CASE("composition") {
    auto translation = make_translation(10, 0);
    auto scale = make_scale(2, 2);
    REQUIRE((translation * scale).map(Point(1, 1)) == Point(12, 2));
    REQUIRE((scale * translation).map(Point(1, 1)) == Point(22, 2));
    auto left = Matrix(2, 3, 5, 7, 11, 13);
    auto right = Matrix(-1, 4, 2, 0.5, -3, 6);
    auto point = Point(1.5, -2.5);
    REQUIRE(is_near((left * right).map(point), left.map(right.map(point))));
  }

  TEST_CASE("compound_assignment") {
    auto matrix = make_translation(10, 0);
    matrix *= make_scale(2, 2);
    REQUIRE(matrix == make_translation(10, 0) * make_scale(2, 2));
  }

  TEST_CASE("determinant") {
    REQUIRE(Matrix(2, 3, 5, 7, 11, 13).get_determinant() == -1);
    REQUIRE(make_scale(2, 4).get_determinant() == 8);
  }

  TEST_CASE("invert") {
    auto matrix = Matrix(2, 3, 5, 7, 11, 13);
    auto inverse = invert(matrix);
    REQUIRE(inverse);
    REQUIRE(is_near(*inverse * matrix, Matrix()));
    REQUIRE(is_near(matrix * *inverse, Matrix()));
    auto rotation = make_rotation(0.3, Point(4, 5));
    auto rotation_inverse = invert(rotation);
    REQUIRE(rotation_inverse);
    REQUIRE(is_near(
      rotation_inverse->map(rotation.map(Point(7, -2))), Point(7, -2)));
  }

  TEST_CASE("singular_inverse") {
    REQUIRE(!invert(make_scale(0, 1)));
    REQUIRE(!invert(Matrix(1, 2, 2, 4, 5, 6)));
    REQUIRE(!invert(make_scale(1e-200, 1e-200)));
  }

  TEST_CASE("map_rect") {
    REQUIRE(make_scale(2, -1).map(Rect(1, 1, 2, 3)) == Rect(2, -4, 4, 3));
    auto rotated = make_rotation(std::numbers::pi / 4).map(Rect(0, 0, 1, 1));
    REQUIRE(rotated.m_x == doctest::Approx(-std::numbers::sqrt2 / 2));
    REQUIRE(rotated.m_y == doctest::Approx(0));
    REQUIRE(rotated.m_width == doctest::Approx(std::numbers::sqrt2));
    REQUIRE(rotated.m_height == doctest::Approx(std::numbers::sqrt2));
  }

  TEST_CASE("stream") {
    auto stream = std::ostringstream();
    stream << Matrix(1, 2, 3, 4, 5.5, -6);
    REQUIRE(stream.str() == "(1, 2, 3, 4, 5.5, -6)");
  }
}
