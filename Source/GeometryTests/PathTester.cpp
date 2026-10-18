#include <cmath>
#include <numbers>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Path.hpp"

using namespace Rellu;

namespace {
  Segment make_segment(SegmentType type, Point first, Point second = Point(),
      Point third = Point()) {
    auto segment = Segment(type);
    segment.m_points[0] = first;
    segment.m_points[1] = second;
    segment.m_points[2] = third;
    return segment;
  }

  bool is_near(const Rect& left, const Rect& right) {
    return std::abs(left.m_x - right.m_x) < 1e-9 &&
      std::abs(left.m_y - right.m_y) < 1e-9 &&
      std::abs(left.m_width - right.m_width) < 1e-9 &&
      std::abs(left.m_height - right.m_height) < 1e-9;
  }

  Path make_square(Point corner, double size) {
    auto path = Path();
    path.move_to(corner);
    path.line_to(corner + Point(size, 0));
    path.line_to(corner + Point(size, size));
    path.line_to(corner + Point(0, size));
    path.close();
    return path;
  }

  Path make_reversed_square(Point corner, double size) {
    auto path = Path();
    path.move_to(corner);
    path.line_to(corner + Point(0, size));
    path.line_to(corner + Point(size, size));
    path.line_to(corner + Point(size, 0));
    path.close();
    return path;
  }

  Path append(Path left, const Path& right) {
    for(auto& segment : right.get_segments()) {
      auto& points = segment.m_points;
      if(segment.m_type == SegmentType::MOVE) {
        left.move_to(points[0]);
      } else if(segment.m_type == SegmentType::LINE) {
        left.line_to(points[0]);
      } else if(segment.m_type == SegmentType::CLOSE) {
        left.close();
      }
    }
    return left;
  }
}

TEST_SUITE("Path") {
  TEST_CASE("empty") {
    auto path = Path();
    REQUIRE(path.is_empty());
    REQUIRE(path.get_segments().empty());
    REQUIRE(path.get_fill_rule() == FillRule::NON_ZERO);
    REQUIRE(Path(FillRule::EVEN_ODD).get_fill_rule() == FillRule::EVEN_ODD);
  }

  TEST_CASE("fill_rule") {
    auto path = Path();
    path.set_fill_rule(FillRule::EVEN_ODD);
    REQUIRE(path.get_fill_rule() == FillRule::EVEN_ODD);
  }

  TEST_CASE("segments") {
    auto path = Path();
    path.move_to(Point(1, 2));
    path.line_to(Point(3, 4));
    path.quadratic_to(Point(5, 6), Point(7, 8));
    path.cubic_to(Point(9, 10), Point(11, 12), Point(13, 14));
    path.close();
    REQUIRE(!path.is_empty());
    REQUIRE(path.get_segments() == std::vector{
      make_segment(SegmentType::MOVE, Point(1, 2)),
      make_segment(SegmentType::LINE, Point(3, 4)),
      make_segment(SegmentType::QUADRATIC, Point(5, 6), Point(7, 8)),
      make_segment(
        SegmentType::CUBIC, Point(9, 10), Point(11, 12), Point(13, 14)),
      make_segment(SegmentType::CLOSE, Point(1, 2))});
  }

  TEST_CASE("repeated_move") {
    auto path = Path();
    path.move_to(Point(1, 2));
    path.move_to(Point(3, 4));
    path.line_to(Point(5, 6));
    path.move_to(Point(7, 8));
    REQUIRE(path.get_segments() == std::vector{
      make_segment(SegmentType::MOVE, Point(3, 4)),
      make_segment(SegmentType::LINE, Point(5, 6)),
      make_segment(SegmentType::MOVE, Point(7, 8))});
  }

  TEST_CASE("implicit_move") {
    auto path = Path();
    path.line_to(Point(1, 2));
    REQUIRE(path.get_segments() == std::vector{
      make_segment(SegmentType::MOVE, Point(0, 0)),
      make_segment(SegmentType::LINE, Point(1, 2))});
  }

  TEST_CASE("drawing_after_close") {
    auto path = Path();
    path.move_to(Point(1, 1));
    path.line_to(Point(5, 1));
    path.close();
    path.cubic_to(Point(2, 2), Point(3, 3), Point(4, 4));
    REQUIRE(path.get_segments() == std::vector{
      make_segment(SegmentType::MOVE, Point(1, 1)),
      make_segment(SegmentType::LINE, Point(5, 1)),
      make_segment(SegmentType::CLOSE, Point(1, 1)),
      make_segment(SegmentType::MOVE, Point(1, 1)),
      make_segment(SegmentType::CUBIC, Point(2, 2), Point(3, 3), Point(4, 4))});
  }

  TEST_CASE("redundant_close") {
    auto path = Path();
    path.close();
    REQUIRE(path.is_empty());
    path.move_to(Point(1, 1));
    path.line_to(Point(2, 2));
    path.close();
    path.close();
    REQUIRE(path.get_segments().size() == 3);
  }

  TEST_CASE("equality") {
    REQUIRE(make_square(Point(0, 0), 1) == make_square(Point(0, 0), 1));
    REQUIRE(make_square(Point(0, 0), 1) != make_square(Point(0, 0), 2));
    auto path = make_square(Point(0, 0), 1);
    path.set_fill_rule(FillRule::EVEN_ODD);
    REQUIRE(path != make_square(Point(0, 0), 1));
  }

  TEST_CASE("transform") {
    auto path = Path(FillRule::EVEN_ODD);
    path.move_to(Point(1, 0));
    path.quadratic_to(Point(2, 0), Point(2, 1));
    path.cubic_to(Point(2, 2), Point(1, 2), Point(0, 2));
    path.close();
    auto transformed = transform(path, make_translation(10, 20));
    auto expected = Path(FillRule::EVEN_ODD);
    expected.move_to(Point(11, 20));
    expected.quadratic_to(Point(12, 20), Point(12, 21));
    expected.cubic_to(Point(12, 22), Point(11, 22), Point(10, 22));
    expected.close();
    REQUIRE(transformed == expected);
  }

  TEST_CASE("line_bounds") {
    auto path = Path();
    path.move_to(Point(5, 10));
    path.line_to(Point(-5, 20));
    path.line_to(Point(15, 0));
    REQUIRE(get_bounds(path) == Rect(-5, 0, 20, 20));
  }

  TEST_CASE("empty_bounds") {
    REQUIRE(get_bounds(Path()) == Rect());
    auto path = Path();
    path.move_to(Point(5, 5));
    REQUIRE(get_bounds(path) == Rect());
  }

  TEST_CASE("move_bounds") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.line_to(Point(1, 1));
    path.move_to(Point(100, 100));
    REQUIRE(get_bounds(path) == Rect(0, 0, 1, 1));
  }

  TEST_CASE("quadratic_bounds") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.quadratic_to(Point(5, 10), Point(10, 0));
    REQUIRE(is_near(get_bounds(path), Rect(0, 0, 10, 5)));
  }

  TEST_CASE("cubic_bounds") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.cubic_to(Point(0, -8), Point(10, -8), Point(10, 0));
    REQUIRE(is_near(get_bounds(path), Rect(0, -6, 10, 6)));
    auto s_curve = Path();
    s_curve.move_to(Point(0, 0));
    s_curve.cubic_to(Point(40, 0), Point(-30, 10), Point(10, 10));
    auto bounds = get_bounds(s_curve);
    REQUIRE(bounds.m_x == doctest::Approx(-2.8334945158758416));
    REQUIRE(bounds.get_right() == doctest::Approx(12.833494515875838));
    REQUIRE(bounds.m_y == 0);
    REQUIRE(bounds.m_height == 10);
  }

  TEST_CASE("transformed_bounds") {
    auto path = make_square(Point(0, 0), 2);
    auto bounds =
      get_bounds(path, make_rotation(std::numbers::pi / 4, Point(1, 1)));
    REQUIRE(is_near(bounds, Rect(1 - std::numbers::sqrt2,
      1 - std::numbers::sqrt2, 2 * std::numbers::sqrt2,
      2 * std::numbers::sqrt2)));
    REQUIRE(get_bounds(path, make_scale(3, 0.5)) == Rect(0, 0, 6, 1));
  }

  TEST_CASE("contains_polygon") {
    auto path = make_square(Point(0, 0), 10);
    REQUIRE(contains(path, Point(5, 5)));
    REQUIRE(contains(path, Point(0.001, 9.999)));
    REQUIRE(!contains(path, Point(-1, 5)));
    REQUIRE(!contains(path, Point(11, 5)));
    REQUIRE(!contains(path, Point(5, -1)));
    REQUIRE(!contains(path, Point(5, 11)));
    REQUIRE(!contains(Path(), Point(0, 0)));
  }

  TEST_CASE("contains_open_subpath") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.line_to(Point(10, 0));
    path.line_to(Point(10, 10));
    REQUIRE(contains(path, Point(8, 2)));
    REQUIRE(!contains(path, Point(2, 8)));
  }

  TEST_CASE("contains_fill_rule") {
    auto same_direction =
      append(make_square(Point(0, 0), 10), make_square(Point(3, 3), 4));
    REQUIRE(contains(same_direction, Point(5, 5), FillRule::NON_ZERO));
    REQUIRE(!contains(same_direction, Point(5, 5), FillRule::EVEN_ODD));
    REQUIRE(contains(same_direction, Point(1, 1), FillRule::NON_ZERO));
    REQUIRE(contains(same_direction, Point(1, 1), FillRule::EVEN_ODD));
    auto opposite_direction = append(
      make_square(Point(0, 0), 10), make_reversed_square(Point(3, 3), 4));
    REQUIRE(!contains(opposite_direction, Point(5, 5), FillRule::NON_ZERO));
    REQUIRE(!contains(opposite_direction, Point(5, 5), FillRule::EVEN_ODD));
    same_direction.set_fill_rule(FillRule::EVEN_ODD);
    REQUIRE(!contains(same_direction, Point(5, 5)));
    same_direction.set_fill_rule(FillRule::NON_ZERO);
    REQUIRE(contains(same_direction, Point(5, 5)));
  }

  TEST_CASE("contains_curve") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.cubic_to(Point(0, -8), Point(10, -8), Point(10, 0));
    path.close();
    REQUIRE(contains(path, Point(5, -5.9)));
    REQUIRE(!contains(path, Point(5, -6.1)));
    REQUIRE(contains(path, Point(0.5, -1)));
    REQUIRE(!contains(path, Point(-0.1, -1)));
    REQUIRE(!contains(path, Point(5, 0.1)));
    auto quadratic = Path();
    quadratic.move_to(Point(0, 0));
    quadratic.quadratic_to(Point(5, 10), Point(10, 0));
    REQUIRE(contains(quadratic, Point(5, 4.9)));
    REQUIRE(!contains(quadratic, Point(5, 5.1)));
    REQUIRE(!contains(quadratic, Point(5, -0.1)));
  }

  TEST_CASE("contains_self_intersection") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.line_to(Point(10, 10));
    path.line_to(Point(10, 0));
    path.line_to(Point(0, 10));
    path.close();
    for(auto rule : {FillRule::NON_ZERO, FillRule::EVEN_ODD}) {
      REQUIRE(contains(path, Point(2, 5), rule));
      REQUIRE(contains(path, Point(8, 5), rule));
      REQUIRE(!contains(path, Point(5, 2), rule));
      REQUIRE(!contains(path, Point(5, 8), rule));
    }
  }
}
