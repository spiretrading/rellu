#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/SvgPath.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"

using namespace Rellu;

TEST_SUITE("ShapeGeometry") {
  TEST_CASE("rectangle") {
    auto bounds = Rect(1, 2, 30, 40);
    REQUIRE(to_path(RectangleShape(bounds)) == make_rectangle(bounds));
    auto radii = CornerRadii(1, 2, 3, 4);
    REQUIRE(to_path(RectangleShape(bounds, radii)) ==
      make_rectangle(bounds, radii));
  }

  TEST_CASE("ellipse") {
    REQUIRE(to_path(EllipseShape(Point(5, 6), 7, 8)) ==
      make_ellipse(Point(5, 6), 7, 8));
  }

  TEST_CASE("line") {
    REQUIRE(to_path(LineShape(Point(1, 1), Point(4, 5))) ==
      make_line(Point(1, 1), Point(4, 5)));
  }

  TEST_CASE("polygon") {
    REQUIRE(to_path(PolygonShape(6, Size(20, 10), 1)) ==
      make_polygon(6, Rect(0, 0, 20, 10), 1));
    auto vertices = std::vector<Point>({Point(0, 0), Point(10, 0),
      Point(5, 8)});
    REQUIRE(to_path(PolygonShape(3, Size(10, 8), 2, 100, vertices)) ==
      make_polygon(vertices, 2));
    REQUIRE(to_path(PolygonShape(5, Size(20, 10), 1, 40)) ==
      make_polygon(5, Rect(0, 0, 20, 10), 1, 40));
  }

  TEST_CASE("path") {
    auto path = *parse_svg_path("M0 0 L10 0 L10 10 Z");
    auto shape = PathShape(Shared(path), FillRule::EVEN_ODD);
    auto result = to_path(shape);
    REQUIRE(result.get_segments() == path.get_segments());
    REQUIRE(result.get_fill_rule() == FillRule::EVEN_ODD);
    REQUIRE(shape.m_path->get_fill_rule() == FillRule::NON_ZERO);
  }

  TEST_CASE("compound") {
    auto path = make_rectangle(Rect(0, 0, 5, 5));
    auto compound = CompoundShape(CompoundOperation::INTERSECT, Shared(path));
    REQUIRE(to_path(compound) == path);
  }
}
