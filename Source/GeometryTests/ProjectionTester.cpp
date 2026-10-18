#include <doctest/doctest.h>
#include "Rellu/Geometry/Projection.hpp"

using namespace Rellu;

namespace {
  bool is_near(Point left, Point right) {
    return distance(left, right) < 1e-9;
  }

  const auto PERSPECTIVE = Projection(2, 0.5, -1, 3, 4, 5, 0.01, -0.02, 1.5);
}

TEST_SUITE("Projection") {
  TEST_CASE("identity") {
    auto projection = Projection();
    REQUIRE(projection.is_affine());
    REQUIRE(projection.map(Point(3, -4)) == Point(3, -4));
    REQUIRE(!PERSPECTIVE.is_affine());
  }

  TEST_CASE("map") {
    auto projection = Projection(1, 0, 0, 1, 0, 0, 0.5, 0, 1);
    REQUIRE(projection.get_divisor(Point(2, 7)) == 2);
    REQUIRE(projection.map(Point(2, 7)) == Point(1, 3.5));
    REQUIRE(projection.map(Rect(0, 0, 2, 2)) == Rect(0, 0, 1, 2));
  }

  TEST_CASE("affine_conversion") {
    auto matrix = Matrix(2, 3, 5, 7, 11, 13);
    auto projection = to_projection(matrix);
    REQUIRE(projection.is_affine());
    REQUIRE(projection.map(Point(2, 3)) == matrix.map(Point(2, 3)));
  }

  TEST_CASE("composition") {
    auto matrix = make_translation(10, 0) * make_scale(2, 3);
    auto point = Point(3, -2);
    REQUIRE(is_near((PERSPECTIVE * matrix).map(point),
      PERSPECTIVE.map(matrix.map(point))));
    REQUIRE(is_near((matrix * PERSPECTIVE).map(point),
      matrix.map(PERSPECTIVE.map(point))));
    REQUIRE(is_near((PERSPECTIVE * PERSPECTIVE).map(point),
      PERSPECTIVE.map(PERSPECTIVE.map(point))));
  }

  TEST_CASE("inversion") {
    auto inverse = invert(PERSPECTIVE);
    REQUIRE(inverse);
    for(auto point : {Point(0, 0), Point(3, -2), Point(-7, 11)}) {
      REQUIRE(is_near(inverse->map(PERSPECTIVE.map(point)), point));
    }
    REQUIRE(!invert(Projection(1, 2, 2, 4, 0, 0, 0, 0, 1)));
  }

  TEST_CASE("linearization") {
    auto point = Point(3, -2);
    auto matrix = linearize(PERSPECTIVE, point);
    REQUIRE(is_near(matrix.map(point), PERSPECTIVE.map(point)));
    auto step = 1e-6;
    auto x = (PERSPECTIVE.map(point + Point(step, 0)) -
      PERSPECTIVE.map(point - Point(step, 0))) / (2 * step);
    auto y = (PERSPECTIVE.map(point + Point(0, step)) -
      PERSPECTIVE.map(point - Point(0, step))) / (2 * step);
    REQUIRE(distance(x, Point(matrix.m_a, matrix.m_b)) < 1e-6);
    REQUIRE(distance(y, Point(matrix.m_c, matrix.m_d)) < 1e-6);
  }

  TEST_CASE("path") {
    auto path = Path(FillRule::EVEN_ODD);
    path.move_to(Point(0, 0));
    path.line_to(Point(10, 0));
    path.quadratic_to(Point(12, 5), Point(10, 10));
    path.cubic_to(Point(8, 12), Point(2, 12), Point(0, 10));
    path.close();
    auto projected = transform(path, PERSPECTIVE);
    REQUIRE(projected.get_fill_rule() == FillRule::EVEN_ODD);
    auto& segments = projected.get_segments();
    REQUIRE(segments.size() == 8);
    REQUIRE(segments[0].m_points[0] == PERSPECTIVE.map(Point(0, 0)));
    REQUIRE(segments[1].m_points[0] == PERSPECTIVE.map(Point(10, 0)));
    REQUIRE(segments[2].m_type == SegmentType::QUADRATIC);
    REQUIRE(segments[2].m_points[0] == PERSPECTIVE.map(Point(12, 5)));
    for(auto i = 3; i != 7; ++i) {
      REQUIRE(segments[i].m_type == SegmentType::CUBIC);
    }
    REQUIRE(is_near(segments[4].m_points[2], PERSPECTIVE.map(Point(5, 11.5))));
    REQUIRE(segments[6].m_points[2] == PERSPECTIVE.map(Point(0, 10)));
    REQUIRE(segments[7].m_type == SegmentType::CLOSE);
  }

  TEST_CASE("arc") {
    auto path = Path();
    path.move_to(Point(0, 10));
    path.arc_to(Point(0, 0), Point(10, 0));
    auto projected = transform(path, PERSPECTIVE);
    auto expected = Path();
    expected.move_to(PERSPECTIVE.map(Point(0, 10)));
    expected.arc_to(
      PERSPECTIVE.map(Point(0, 0)), PERSPECTIVE.map(Point(10, 0)));
    REQUIRE(projected.get_segments().size() == 2);
    REQUIRE(projected.get_segments()[1].m_is_arc);
    REQUIRE(is_near(projected.get_segments()[1].m_points[0],
      expected.get_segments()[1].m_points[0]));
    REQUIRE(is_near(projected.get_segments()[1].m_points[1],
      expected.get_segments()[1].m_points[1]));
  }
}
