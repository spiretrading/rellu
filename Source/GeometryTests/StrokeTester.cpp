#include <cmath>
#include <numbers>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Polyline.hpp"
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Geometry/Stroke.hpp"

using namespace Rellu;

namespace {
  constexpr auto PI = std::numbers::pi;

  StrokeGeometry make_stroke(double width, StrokeAlignment alignment,
      StrokeCap cap, StrokeJoin join, double miter_limit = 10) {
    return StrokeGeometry(width, alignment, cap, join, miter_limit);
  }

  StrokeGeometry make_stroke(double width, StrokeCap cap) {
    return make_stroke(width, StrokeAlignment::CENTER, cap, StrokeJoin::MITER);
  }

  StrokeGeometry make_stroke(
      double width, StrokeJoin join, double miter_limit = 10) {
    return make_stroke(
      width, StrokeAlignment::CENTER, StrokeCap::BUTT, join, miter_limit);
  }

  StrokeGeometry make_stroke(double width, StrokeAlignment alignment) {
    return make_stroke(width, alignment, StrokeCap::BUTT, StrokeJoin::MITER);
  }

  StrokeGeometry make_dashed_stroke(double width, StrokeCap cap,
      std::vector<double> dashes, double offset) {
    auto stroke = make_stroke(width, cap);
    stroke.m_dashes = dashes;
    stroke.m_dash_offset = offset;
    return stroke;
  }

  double get_area(const Path& path) {
    auto area = 0.0;
    for(auto& polyline : flatten(path, 0.01)) {
      auto& points = polyline.m_points;
      for(auto i = std::size_t(0); i != points.size(); ++i) {
        area += cross(points[i], points[(i + 1) % points.size()]);
      }
    }
    return area / 2;
  }

  int count_subpaths(const Path& path) {
    auto count = 0;
    for(auto& segment : path.get_segments()) {
      if(segment.m_type == SegmentType::MOVE) {
        ++count;
      }
    }
    return count;
  }

  bool is_near(const Rect& left, const Rect& right, double tolerance) {
    return distance(left.get_top_left(), right.get_top_left()) < tolerance &&
      distance(left.get_bottom_right(), right.get_bottom_right()) <
        tolerance;
  }

  Path make_path(std::vector<Point> points, bool is_closed) {
    auto path = Path();
    path.move_to(points.front());
    for(auto i = std::size_t(1); i != points.size(); ++i) {
      path.line_to(points[i]);
    }
    if(is_closed) {
      path.close();
    }
    return path;
  }

  Path make_ring(FillRule rule) {
    auto path = make_rectangle(Rect(0, 0, 10, 10));
    path.move_to(Point(3, 3));
    path.line_to(Point(7, 3));
    path.line_to(Point(7, 7));
    path.line_to(Point(3, 7));
    path.close();
    path.set_fill_rule(rule);
    return path;
  }
}

TEST_SUITE("StrokeGeometry") {
  TEST_CASE("caps") {
    auto line = make_line(Point(0, 0), Point(10, 0));
    auto butt = outline_stroke(line, make_stroke(2, StrokeCap::BUTT), 0.01);
    REQUIRE(butt.get_fill_rule() == FillRule::NON_ZERO);
    REQUIRE(count_subpaths(butt) == 1);
    REQUIRE(get_area(butt) == doctest::Approx(20));
    REQUIRE(get_bounds(butt) == Rect(0, -1, 10, 2));
    REQUIRE(contains(butt, Point(5, 0.99)));
    REQUIRE(!contains(butt, Point(5, 1.01)));
    auto square =
      outline_stroke(line, make_stroke(2, StrokeCap::SQUARE), 0.01);
    REQUIRE(get_area(square) == doctest::Approx(24));
    REQUIRE(get_bounds(square) == Rect(-1, -1, 12, 2));
    auto round =
      outline_stroke(line, make_stroke(2, StrokeCap::ROUND), 0.001);
    REQUIRE(count_subpaths(round) == 1);
    REQUIRE(get_area(round) == doctest::Approx(20 + PI).epsilon(2e-3));
    REQUIRE(is_near(get_bounds(round), Rect(-1, -1, 12, 2), 0.01));
    REQUIRE(contains(round, Point(-0.6, 0.6)));
    REQUIRE(!contains(round, Point(-0.9, 0.9)));
  }

  TEST_CASE("rectangle_alignment") {
    auto rectangle = make_rectangle(Rect(0, 0, 10, 10));
    auto center = outline_stroke(
      rectangle, make_stroke(2, StrokeAlignment::CENTER), 0.01);
    REQUIRE(count_subpaths(center) == 2);
    REQUIRE(get_area(center) == doctest::Approx(80));
    REQUIRE(get_bounds(center) == Rect(-1, -1, 12, 12));
    REQUIRE(contains(center, Point(0.5, 5)));
    REQUIRE(contains(center, Point(-0.5, -0.5)));
    REQUIRE(!contains(center, Point(5, 5)));
    auto inside = outline_stroke(
      rectangle, make_stroke(2, StrokeAlignment::INSIDE), 0.01);
    REQUIRE(count_subpaths(inside) == 2);
    REQUIRE(get_area(inside) == doctest::Approx(64));
    REQUIRE(get_bounds(inside) == Rect(0, 0, 10, 10));
    REQUIRE(contains(inside, Point(1.5, 5)));
    REQUIRE(!contains(inside, Point(2.5, 5)));
    REQUIRE(!contains(inside, Point(-0.5, 5)));
    auto outside = outline_stroke(
      rectangle, make_stroke(2, StrokeAlignment::OUTSIDE), 0.01);
    REQUIRE(count_subpaths(outside) == 2);
    REQUIRE(get_area(outside) == doctest::Approx(96));
    REQUIRE(get_bounds(outside) == Rect(-2, -2, 14, 14));
    REQUIRE(contains(outside, Point(-1.5, 5)));
    REQUIRE(contains(outside, Point(-1.9, -1.9)));
    REQUIRE(!contains(outside, Point(0.5, 5)));
  }

  TEST_CASE("rectangle_joins") {
    auto rectangle = make_rectangle(Rect(0, 0, 10, 10));
    REQUIRE(get_area(outline_stroke(
      rectangle, make_stroke(2, StrokeJoin::BEVEL), 0.01)) ==
        doctest::Approx(78));
    REQUIRE(get_area(outline_stroke(
      rectangle, make_stroke(2, StrokeJoin::ROUND), 0.01)) ==
        doctest::Approx(76 + PI).epsilon(1e-3));
    REQUIRE(get_area(outline_stroke(rectangle, make_stroke(2,
      StrokeAlignment::INSIDE, StrokeCap::BUTT, StrokeJoin::ROUND), 0.01)) ==
        doctest::Approx(64));
    REQUIRE(get_area(outline_stroke(rectangle, make_stroke(2,
      StrokeAlignment::OUTSIDE, StrokeCap::BUTT, StrokeJoin::ROUND),
        0.01)) == doctest::Approx(80 + 4 * PI).epsilon(1e-3));
    REQUIRE(get_area(outline_stroke(rectangle, make_stroke(2,
      StrokeAlignment::OUTSIDE, StrokeCap::BUTT, StrokeJoin::BEVEL),
        0.01)) == doctest::Approx(88));
  }

  TEST_CASE("circle_alignment") {
    auto circle = make_circle(Point(0, 0), 10);
    auto center = outline_stroke(
      circle, make_stroke(2, StrokeAlignment::CENTER), 0.01);
    REQUIRE(count_subpaths(center) == 2);
    REQUIRE(get_area(center) == doctest::Approx(40 * PI).epsilon(1e-3));
    auto inside = outline_stroke(
      circle, make_stroke(2, StrokeAlignment::INSIDE), 0.01);
    REQUIRE(count_subpaths(inside) == 2);
    REQUIRE(get_area(inside) == doctest::Approx(36 * PI).epsilon(1e-3));
    REQUIRE(contains(inside, Point(8.5, 0)));
    REQUIRE(!contains(inside, Point(7.5, 0)));
    REQUIRE(!contains(inside, Point(10.1, 0)));
    auto outside = outline_stroke(
      circle, make_stroke(2, StrokeAlignment::OUTSIDE), 0.01);
    REQUIRE(count_subpaths(outside) == 2);
    REQUIRE(get_area(outside) == doctest::Approx(44 * PI).epsilon(1e-3));
    REQUIRE(contains(outside, Point(0, -11.5)));
    REQUIRE(!contains(outside, Point(0, -9.9)));
    REQUIRE(!contains(outside, Point(0, -12.1)));
  }

  TEST_CASE("joins") {
    auto corner =
      make_path({Point(0, 0), Point(10, 0), Point(10, 10)}, false);
    auto miter =
      outline_stroke(corner, make_stroke(2, StrokeJoin::MITER), 0.01);
    REQUIRE(get_area(miter) == doctest::Approx(40));
    REQUIRE(get_bounds(miter) == Rect(0, -1, 11, 11));
    REQUIRE(get_area(outline_stroke(
      corner, make_stroke(2, StrokeJoin::BEVEL), 0.01)) ==
        doctest::Approx(39.5));
    REQUIRE(get_area(outline_stroke(
      corner, make_stroke(2, StrokeJoin::ROUND), 0.01)) ==
        doctest::Approx(39 + PI / 4).epsilon(1e-3));
  }

  TEST_CASE("miter_limit") {
    auto half_width = 10 * std::sin(PI / 6);
    auto height = 10 * std::cos(PI / 6);
    auto wedge = make_path({Point(-half_width, height), Point(0, 0),
      Point(half_width, height)}, false);
    REQUIRE(get_bounds(outline_stroke(
      wedge, make_stroke(2, StrokeJoin::MITER, 2.5), 0.01)).m_y ==
        doctest::Approx(-2).epsilon(1e-3));
    REQUIRE(get_bounds(outline_stroke(
      wedge, make_stroke(2, StrokeJoin::MITER, 1.9), 0.01)).m_y ==
        doctest::Approx(-0.5).epsilon(1e-3));
    REQUIRE(get_bounds(outline_stroke(
      wedge, make_stroke(2, StrokeJoin::BEVEL), 0.01)).m_y ==
        doctest::Approx(-0.5).epsilon(1e-3));
    REQUIRE(get_bounds(outline_stroke(
      wedge, make_stroke(2, StrokeJoin::ROUND), 0.001)).m_y ==
        doctest::Approx(-1).epsilon(2e-3));
    auto rectangle = make_rectangle(Rect(0, 0, 10, 10));
    REQUIRE(get_area(outline_stroke(
      rectangle, make_stroke(2, StrokeJoin::MITER, 1.5), 0.01)) ==
        doctest::Approx(80));
    REQUIRE(get_area(outline_stroke(
      rectangle, make_stroke(2, StrokeJoin::MITER, 1.4), 0.01)) ==
        doctest::Approx(78));
  }

  TEST_CASE("smooth_vertices") {
    auto rectangle =
      make_rectangle(Rect(0, 0, 40, 40), CornerRadii(2, 2, 2, 2));
    REQUIRE(get_area(outline_stroke(
      rectangle, make_stroke(10, StrokeJoin::MITER), 0.01)) ==
        doctest::Approx(1404 + 49 * PI).epsilon(1e-3));
    auto circle = make_circle(Point(0, 0), 1);
    REQUIRE(outline_stroke(circle, make_stroke(10, StrokeJoin::MITER), 0.1) ==
      outline_stroke(circle, make_stroke(10, StrokeJoin::ROUND), 0.1));
  }

  TEST_CASE("dashes") {
    auto line = make_line(Point(0, 0), Point(100, 0));
    auto dashed = outline_stroke(line,
      make_dashed_stroke(2, StrokeCap::BUTT, {10, 5}, 0), 0.01);
    REQUIRE(count_subpaths(dashed) == 7);
    REQUIRE(get_area(dashed) == doctest::Approx(140));
    REQUIRE(get_bounds(dashed) == Rect(0, -1, 100, 2));
    REQUIRE(contains(dashed, Point(9, 0)));
    REQUIRE(!contains(dashed, Point(11, 0)));
    REQUIRE(contains(dashed, Point(16, 0)));
    auto offset = outline_stroke(line,
      make_dashed_stroke(2, StrokeCap::BUTT, {10, 5}, 5), 0.01);
    REQUIRE(count_subpaths(offset) == 7);
    REQUIRE(get_area(offset) == doctest::Approx(130));
    REQUIRE(contains(offset, Point(4, 0)));
    REQUIRE(!contains(offset, Point(6, 0)));
    REQUIRE(contains(offset, Point(11, 0)));
    REQUIRE(outline_stroke(line,
      make_dashed_stroke(2, StrokeCap::BUTT, {10, 5}, -10), 0.01) ==
        offset);
    auto odd = outline_stroke(
      line, make_dashed_stroke(2, StrokeCap::BUTT, {10}, 0), 0.01);
    REQUIRE(count_subpaths(odd) == 5);
    REQUIRE(get_area(odd) == doctest::Approx(100));
    auto round = outline_stroke(line,
      make_dashed_stroke(2, StrokeCap::ROUND, {10, 5}, 0), 0.001);
    REQUIRE(count_subpaths(round) == 7);
    REQUIRE(get_area(round) == doctest::Approx(7 * (20 + PI)).epsilon(2e-3));
    REQUIRE(count_subpaths(outline_stroke(line,
      make_dashed_stroke(2, StrokeCap::ROUND, {10, 10}, 0), 0.01)) == 5);
    auto solid = outline_stroke(line, make_stroke(2, StrokeCap::BUTT), 0.01);
    REQUIRE(outline_stroke(line,
      make_dashed_stroke(2, StrokeCap::BUTT, {0, 0}, 0), 0.01) == solid);
    REQUIRE(outline_stroke(line,
      make_dashed_stroke(2, StrokeCap::BUTT, {10, -5}, 0), 0.01) == solid);
  }

  TEST_CASE("zero_length_dashes") {
    auto line = make_line(Point(0, 0), Point(100, 0));
    auto dots = outline_stroke(line,
      make_dashed_stroke(2, StrokeCap::ROUND, {0, 10}, 0), 0.001);
    REQUIRE(count_subpaths(dots) == 11);
    REQUIRE(get_area(dots) == doctest::Approx(11 * PI).epsilon(2e-3));
    auto squares = outline_stroke(line,
      make_dashed_stroke(2, StrokeCap::SQUARE, {0, 10}, 0), 0.01);
    REQUIRE(count_subpaths(squares) == 11);
    REQUIRE(get_area(squares) == doctest::Approx(44));
    REQUIRE(outline_stroke(line,
      make_dashed_stroke(2, StrokeCap::BUTT, {0, 10}, 0), 0.01).is_empty());
  }

  TEST_CASE("closed_dashes") {
    auto rectangle = make_rectangle(Rect(0, 0, 10, 10));
    auto dashed = outline_stroke(rectangle,
      make_dashed_stroke(2, StrokeCap::BUTT, {10, 10}, 5), 0.01);
    REQUIRE(count_subpaths(dashed) == 2);
    REQUIRE(get_area(dashed) == doctest::Approx(40));
    REQUIRE(contains(dashed, Point(-0.5, -0.5)));
    REQUIRE(!contains(dashed, Point(10.5, -0.5)));
    REQUIRE(outline_stroke(rectangle,
      make_dashed_stroke(2, StrokeCap::BUTT, {100, 10}, 0), 0.01) ==
        outline_stroke(rectangle, make_stroke(2, StrokeCap::BUTT), 0.01));
    auto inside = make_dashed_stroke(2, StrokeCap::BUTT, {10, 10}, 5);
    inside.m_alignment = StrokeAlignment::INSIDE;
    REQUIRE(get_area(outline_stroke(rectangle, inside, 0.01)) ==
      doctest::Approx(32));
  }

  TEST_CASE("open_alignment") {
    auto corner =
      make_path({Point(0, 0), Point(10, 0), Point(10, 10)}, false);
    auto center = outline_stroke(
      corner, make_stroke(2, StrokeAlignment::CENTER), 0.01);
    REQUIRE(outline_stroke(
      corner, make_stroke(2, StrokeAlignment::INSIDE), 0.01) == center);
    REQUIRE(outline_stroke(
      corner, make_stroke(2, StrokeAlignment::OUTSIDE), 0.01) == center);
  }

  TEST_CASE("fill_rule") {
    REQUIRE(get_area(outline_stroke(make_ring(FillRule::EVEN_ODD),
      make_stroke(1, StrokeAlignment::INSIDE), 0.01)) ==
        doctest::Approx(56));
    REQUIRE(get_area(outline_stroke(make_ring(FillRule::NON_ZERO),
      make_stroke(1, StrokeAlignment::INSIDE), 0.01)) ==
        doctest::Approx(68));
  }

  TEST_CASE("overlap") {
    auto twice = make_rectangle(Rect(0, 0, 10, 10));
    twice.move_to(Point(0, 0));
    twice.line_to(Point(10, 0));
    twice.line_to(Point(10, 10));
    twice.line_to(Point(0, 10));
    twice.close();
    REQUIRE(get_area(outline_stroke(
      twice, make_stroke(2, StrokeAlignment::CENTER), 0.01)) ==
        doctest::Approx(80));
    REQUIRE(get_area(outline_stroke(
      twice, make_stroke(2, StrokeAlignment::INSIDE), 0.01)) ==
        doctest::Approx(64));
    auto loop = make_path({Point(0, 0), Point(10, 0), Point(10, 10),
      Point(5, 10), Point(5, -10)}, false);
    REQUIRE(get_area(outline_stroke(loop, make_stroke(2, StrokeCap::BUTT),
      0.01)) == doctest::Approx(86));
  }

  TEST_CASE("degenerate") {
    auto line = make_line(Point(0, 0), Point(10, 0));
    REQUIRE(outline_stroke(line, make_stroke(0, StrokeCap::ROUND), 0.01).
      is_empty());
    REQUIRE(outline_stroke(line, make_stroke(-1, StrokeCap::ROUND), 0.01).
      is_empty());
    REQUIRE(outline_stroke(Path(), make_stroke(2, StrokeCap::ROUND), 0.01).
      is_empty());
    auto move = Path();
    move.move_to(Point(5, 5));
    REQUIRE(outline_stroke(move, make_stroke(2, StrokeCap::ROUND), 0.01).
      is_empty());
    auto point = Path();
    point.move_to(Point(5, 5));
    point.close();
    auto dot = outline_stroke(point, make_stroke(2, StrokeCap::ROUND), 0.001);
    REQUIRE(count_subpaths(dot) == 1);
    REQUIRE(get_area(dot) == doctest::Approx(PI).epsilon(2e-3));
    REQUIRE(is_near(get_bounds(dot), Rect(4, 4, 2, 2), 0.01));
    auto segment = make_line(Point(5, 5), Point(5, 5));
    auto square =
      outline_stroke(segment, make_stroke(2, StrokeCap::SQUARE), 0.01);
    REQUIRE(get_area(square) == doctest::Approx(4));
    REQUIRE(get_bounds(square) == Rect(4, 4, 2, 2));
    REQUIRE(outline_stroke(segment, make_stroke(2, StrokeCap::BUTT), 0.01).
      is_empty());
  }

  TEST_CASE("tolerance") {
    auto circle = make_circle(Point(0, 0), 10);
    auto previous_error = 0.0;
    auto previous_count = std::size_t(0);
    for(auto tolerance : {1.0, 0.1, 0.01}) {
      auto stroke = outline_stroke(
        circle, make_stroke(2, StrokeAlignment::CENTER), tolerance);
      auto error = std::abs(get_area(stroke) - 40 * PI);
      REQUIRE(error < 40 * PI * tolerance);
      if(previous_count != 0) {
        REQUIRE(error < previous_error);
        REQUIRE(stroke.get_segments().size() > previous_count);
      }
      previous_error = error;
      previous_count = stroke.get_segments().size();
      auto bounds = get_bounds(stroke);
      REQUIRE(bounds.m_x >= -11 - tolerance);
      REQUIRE(bounds.m_x <= -11 + tolerance);
    }
  }
}
