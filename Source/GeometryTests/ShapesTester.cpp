#include <cmath>
#include <numbers>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"

using namespace Rellu;

namespace {
  constexpr auto KAPPA = 4 * (std::numbers::sqrt2 - 1) / 3;

  bool is_near(Point left, Point right, double tolerance = 1e-9) {
    return distance(left, right) < tolerance;
  }

  bool is_near(const Rect& left, const Rect& right) {
    return is_near(left.get_top_left(), right.get_top_left()) &&
      is_near(left.get_bottom_right(), right.get_bottom_right());
  }

  bool is_near(const Path& left, const Path& right) {
    auto& left_segments = left.get_segments();
    auto& right_segments = right.get_segments();
    if(left_segments.size() != right_segments.size()) {
      return false;
    }
    for(auto i = std::size_t(0); i != left_segments.size(); ++i) {
      if(left_segments[i].m_type != right_segments[i].m_type) {
        return false;
      }
      for(auto j = std::size_t(0); j != 3; ++j) {
        if(!is_near(left_segments[i].m_points[j],
            right_segments[i].m_points[j])) {
          return false;
        }
      }
    }
    return true;
  }

  std::vector<SegmentType> get_types(const Path& path) {
    auto types = std::vector<SegmentType>();
    for(auto& segment : path.get_segments()) {
      types.push_back(segment.m_type);
    }
    return types;
  }

  std::vector<Point> get_vertices(const Path& path) {
    auto vertices = std::vector<Point>();
    for(auto& segment : path.get_segments()) {
      if(segment.m_type == SegmentType::MOVE ||
          segment.m_type == SegmentType::LINE) {
        vertices.push_back(segment.m_points[0]);
      }
    }
    return vertices;
  }
}

TEST_SUITE("Shapes") {
  TEST_CASE("clamp_corner_radii") {
    auto scaled = clamp_corner_radii(CornerRadii(5, 1, -2, 3), Size(4, 10));
    REQUIRE(scaled.m_top_left == doctest::Approx(10.0 / 3));
    REQUIRE(scaled.m_top_right == doctest::Approx(2.0 / 3));
    REQUIRE(scaled.m_bottom_right == 0);
    REQUIRE(scaled.m_bottom_left == doctest::Approx(2));
    REQUIRE(clamp_corner_radii(CornerRadii(3, 3, 0, 0), Size(80, 3)) ==
      CornerRadii(3, 3, 0, 0));
    REQUIRE(clamp_corner_radii(CornerRadii(2, 2, 2, 2), Size(80, 3)) ==
      CornerRadii(1.5, 1.5, 1.5, 1.5));
    REQUIRE(clamp_corner_radii(CornerRadii(1, 2, 3, 4), Size(10, 10)) ==
      CornerRadii(1, 2, 3, 4));
    REQUIRE(clamp_corner_radii(CornerRadii(1, 2, 3, 4), Size(-1, 10)) ==
      CornerRadii(0, 0, 0, 0));
  }

  TEST_CASE("rectangle") {
    auto expected = Path();
    expected.move_to(Point(1, 2));
    expected.line_to(Point(4, 2));
    expected.line_to(Point(4, 6));
    expected.line_to(Point(1, 6));
    expected.close();
    REQUIRE(make_rectangle(Rect(1, 2, 3, 4)) == expected);
    REQUIRE(make_rectangle(Rect(1, 2, 3, 4), CornerRadii(0, 0, 0, 0)) ==
      expected);
  }

  TEST_CASE("rounded_rectangle") {
    auto bounds = Rect(0, 0, 100, 50);
    auto path = make_rectangle(bounds, CornerRadii(10, 20, 0, 5));
    auto expected = Path();
    expected.move_to(Point(0, 45));
    expected.line_to(Point(0, 10));
    expected.cubic_to(
      Point(0, 10 - 10 * KAPPA), Point(10 - 10 * KAPPA, 0), Point(10, 0));
    expected.line_to(Point(80, 0));
    expected.cubic_to(Point(80 + 20 * KAPPA, 0), Point(100, 20 - 20 * KAPPA),
      Point(100, 20));
    expected.line_to(Point(100, 50));
    expected.cubic_to(Point(100, 50), Point(100, 50), Point(100, 50));
    expected.line_to(Point(5, 50));
    expected.cubic_to(Point(5 - 5 * KAPPA, 50), Point(0, 45 + 5 * KAPPA),
      Point(0, 45));
    expected.close();
    REQUIRE(is_near(path, expected));
    for(auto& segment : path.get_segments()) {
      REQUIRE(segment.m_is_arc == (segment.m_type == SegmentType::CUBIC));
    }
    REQUIRE(is_near(get_bounds(path), bounds));
    REQUIRE(!contains(path, Point(1, 1)));
    REQUIRE(contains(path, Point(5, 5)));
    REQUIRE(contains(path, Point(99.5, 49.5)));
    REQUIRE(!contains(path, Point(99, 1)));
  }

  TEST_CASE("oversized_radii") {
    auto bounds = Rect(0, 0, 10, 4);
    auto path = make_rectangle(bounds, CornerRadii(5, 5, 5, 5));
    REQUIRE(is_near(path, make_rectangle(bounds, CornerRadii(2, 2, 2, 2))));
    REQUIRE(get_types(path) == std::vector{SegmentType::MOVE,
      SegmentType::LINE, SegmentType::CUBIC, SegmentType::LINE,
      SegmentType::CUBIC, SegmentType::LINE, SegmentType::CUBIC,
      SegmentType::LINE, SegmentType::CUBIC, SegmentType::CLOSE});
    REQUIRE(is_near(get_bounds(path), bounds));
    auto circle = make_rectangle(Rect(0, 0, 10, 10), CornerRadii(5, 5, 5, 5));
    REQUIRE(get_types(circle) == std::vector{SegmentType::MOVE,
      SegmentType::CUBIC, SegmentType::CUBIC, SegmentType::CUBIC,
      SegmentType::CUBIC, SegmentType::CLOSE});
    REQUIRE(circle.get_segments()[0].m_points[0] == Point(0, 5));
    REQUIRE(is_near(get_bounds(circle), Rect(0, 0, 10, 10)));
  }

  TEST_CASE("ellipse") {
    auto path = make_ellipse(Point(10, 20), 5, 3);
    auto& segments = path.get_segments();
    REQUIRE(get_types(path) == std::vector{SegmentType::MOVE,
      SegmentType::CUBIC, SegmentType::CUBIC, SegmentType::CUBIC,
      SegmentType::CUBIC, SegmentType::CLOSE});
    REQUIRE(segments[0].m_points[0] == Point(15, 20));
    for(auto i = std::size_t(1); i != 5; ++i) {
      REQUIRE(segments[i].m_is_arc);
    }
    REQUIRE(segments[1].m_points[2] == Point(10, 23));
    REQUIRE(segments[2].m_points[2] == Point(5, 20));
    REQUIRE(segments[3].m_points[2] == Point(10, 17));
    REQUIRE(segments[4].m_points[2] == Point(15, 20));
    REQUIRE(is_near(get_bounds(path), Rect(5, 17, 10, 6)));
    auto& first = segments[1].m_points;
    auto middle = (Point(15, 20) + 3 * first[0] + 3 * first[1] + first[2]) / 8;
    auto normalized = Point((middle.m_x - 10) / 5, (middle.m_y - 20) / 3);
    REQUIRE(length(normalized) == doctest::Approx(1).epsilon(0.001));
    REQUIRE(middle.m_x > 10);
    REQUIRE(middle.m_y > 20);
    REQUIRE(contains(path, Point(10, 20)));
    REQUIRE(contains(path, Point(14.5, 20)));
    REQUIRE(!contains(path, Point(14.5, 22.5)));
  }

  TEST_CASE("circle") {
    REQUIRE(make_circle(Point(1, 2), 3) == make_ellipse(Point(1, 2), 3, 3));
  }

  TEST_CASE("line") {
    auto expected = Path();
    expected.move_to(Point(0, 0));
    expected.line_to(Point(6, 6));
    REQUIRE(make_line(Point(0, 0), Point(6, 6)) == expected);
  }

  TEST_CASE("polygon_vertices") {
    auto vertices = std::vector{Point(0, 0), Point(10, 0), Point(5, 8)};
    auto expected = Path();
    expected.move_to(Point(0, 0));
    expected.line_to(Point(10, 0));
    expected.line_to(Point(5, 8));
    expected.close();
    REQUIRE(make_polygon(vertices, 0) == expected);
    REQUIRE(make_polygon(std::vector<Point>(), 3).is_empty());
  }

  TEST_CASE("triangle") {
    auto vertices = get_vertices(make_polygon(3, Rect(0, 0, 10, 5), 0));
    REQUIRE(vertices.size() == 3);
    REQUIRE(is_near(vertices[0], Point(5, 0)));
    REQUIRE(is_near(vertices[1], Point(10, 5)));
    REQUIRE(is_near(vertices[2], Point(0, 5)));
    auto stretched = get_vertices(make_polygon(
      3, Rect(0, 0, 15.761641502380371, 7.000156879425049), 0));
    REQUIRE(stretched.size() == 3);
    REQUIRE(is_near(stretched[0], Point(7.880820274353027, 0), 1e-5));
    REQUIRE(is_near(
      stretched[1], Point(15.761641502380371, 7.000157356262207), 1e-5));
    REQUIRE(is_near(stretched[2], Point(0, 7.000157356262207), 1e-5));
  }

  TEST_CASE("regular_polygon") {
    auto square = get_vertices(make_polygon(4, Rect(0, 0, 8, 6), 0));
    REQUIRE(square.size() == 4);
    REQUIRE(is_near(square[0], Point(8, 0)));
    REQUIRE(is_near(square[1], Point(8, 6)));
    REQUIRE(is_near(square[2], Point(0, 6)));
    REQUIRE(is_near(square[3], Point(0, 0)));
    auto hexagon = get_vertices(make_polygon(6, Rect(10, 20, 8, 4), 0));
    REQUIRE(hexagon.size() == 6);
    REQUIRE(is_near(hexagon[0], Point(16, 20)));
    REQUIRE(is_near(hexagon[1], Point(18, 22)));
    REQUIRE(is_near(hexagon[2], Point(16, 24)));
    REQUIRE(is_near(hexagon[3], Point(12, 24)));
    REQUIRE(is_near(hexagon[4], Point(10, 22)));
    REQUIRE(is_near(hexagon[5], Point(12, 20)));
    auto pentagon = make_polygon_vertices(5, Rect(0, 0, 10, 10));
    REQUIRE(pentagon.size() == 5);
    REQUIRE(is_near(pentagon[0], Point(5, 0)));
    REQUIRE(is_near(pentagon[2], Point(pentagon[2].m_x, 10)));
    REQUIRE(is_near(pentagon[3], Point(10 - pentagon[2].m_x, 10)));
    auto bounds = Rect(1, 2, 30, 40);
    REQUIRE(is_near(get_bounds(make_polygon(5, bounds, 0)), bounds));
    REQUIRE(make_polygon(2, bounds, 0).is_empty());
    REQUIRE(make_polygon_vertices(2, bounds).empty());
  }

  TEST_CASE("xd_hexagon") {
    auto vertices = make_polygon_vertices(6, Rect(0, 0, 80, 80));
    REQUIRE(vertices.size() == 6);
    REQUIRE(is_near(vertices[0], Point(60, 0)));
    REQUIRE(is_near(vertices[1], Point(80, 40)));
    REQUIRE(is_near(vertices[2], Point(60, 80)));
    REQUIRE(is_near(vertices[3], Point(20, 80)));
    REQUIRE(is_near(vertices[4], Point(0, 40)));
    REQUIRE(is_near(vertices[5], Point(20, 0)));
  }

  TEST_CASE("rounded_polygon") {
    auto square =
      std::vector{Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)};
    auto handle = 2 * KAPPA;
    auto expected = Path();
    expected.move_to(Point(0, 2));
    expected.cubic_to(Point(0, 2 - handle), Point(2 - handle, 0), Point(2, 0));
    expected.line_to(Point(8, 0));
    expected.cubic_to(
      Point(8 + handle, 0), Point(10, 2 - handle), Point(10, 2));
    expected.line_to(Point(10, 8));
    expected.cubic_to(
      Point(10, 8 + handle), Point(8 + handle, 10), Point(8, 10));
    expected.line_to(Point(2, 10));
    expected.cubic_to(
      Point(2 - handle, 10), Point(0, 8 + handle), Point(0, 8));
    expected.close();
    auto polygon = make_polygon(square, 2);
    REQUIRE(is_near(polygon, expected));
    for(auto& segment : polygon.get_segments()) {
      REQUIRE(!segment.m_is_arc);
    }
    auto bounds = Rect(0, 0, 10, 5);
    auto triangle = make_polygon(3, bounds, 1);
    REQUIRE(bounds.contains(get_bounds(triangle)));
    REQUIRE(!contains(triangle, Point(5, 0.1)));
    REQUIRE(contains(triangle, Point(5, 3)));
  }

  TEST_CASE("oversized_polygon_radius") {
    auto vertices = std::vector{Point(0, 0), Point(10, 0), Point(0, 10)};
    auto path = make_polygon(vertices, 1000);
    auto& segments = path.get_segments();
    REQUIRE(segments.front().m_type == SegmentType::MOVE);
    REQUIRE(is_near(segments.front().m_points[0], Point(0, 5)));
    auto bounds = get_bounds(path);
    REQUIRE(bounds.m_x >= 0);
    REQUIRE(bounds.m_y >= 0);
    REQUIRE(bounds.get_right() <= 10);
    REQUIRE(bounds.get_bottom() <= 10);
    REQUIRE(contains(path, Point(2, 2)));
  }
}
