#include <cmath>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Contour.hpp"
#include "Rellu/Geometry/Shapes.hpp"

using namespace Rellu;

namespace {
  bool is_near(Point left, Point right) {
    return distance(left, right) < 1e-9;
  }

  Contour make_curve() {
    auto contour = Contour();
    contour.m_anchors.push_back(
      Anchor(Point(0, 0), Point(0, 0), Point(0, -40)));
    contour.m_anchors.push_back(
      Anchor(Point(100, 0), Point(100, -40), Point(100, 0)));
    return contour;
  }
}

TEST_SUITE("Contour") {
  TEST_CASE("smooth") {
    REQUIRE(!is_smooth(make_anchor(Point(1, 1))));
    REQUIRE(is_smooth(Anchor(Point(0, 0), Point(-10, 0), Point(5, 0))));
    REQUIRE(!is_smooth(Anchor(Point(0, 0), Point(-10, 0), Point(5, 5))));
    REQUIRE(!is_smooth(Anchor(Point(0, 0), Point(0, 0), Point(5, 0))));
    REQUIRE(!is_smooth(Anchor(Point(0, 0), Point(10, 0), Point(5, 0))));
  }

  TEST_CASE("path_conversion") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.line_to(Point(10, 0));
    path.cubic_to(Point(15, 0), Point(20, 5), Point(20, 10));
    path.quadratic_to(Point(20, 20), Point(10, 20));
    path.line_to(Point(0, 0));
    path.close();
    path.move_to(Point(50, 50));
    path.line_to(Point(60, 50));
    auto contours = to_contours(path);
    REQUIRE(contours.size() == 2);
    auto& square = contours[0];
    REQUIRE(square.m_is_closed);
    REQUIRE(square.m_anchors.size() == 4);
    REQUIRE(square.m_anchors[0] == make_anchor(Point(0, 0)));
    REQUIRE(square.m_anchors[1] ==
      Anchor(Point(10, 0), Point(10, 0), Point(15, 0)));
    REQUIRE(square.m_anchors[2].m_in == Point(20, 5));
    REQUIRE(is_near(square.m_anchors[2].m_out,
      Point(20, 10) + (Point(20, 20) - Point(20, 10)) * (2.0 / 3)));
    REQUIRE(is_near(square.m_anchors[3].m_in,
      Point(10, 20) + (Point(20, 20) - Point(10, 20)) * (2.0 / 3)));
    REQUIRE(!contours[1].m_is_closed);
    REQUIRE(contours[1].m_anchors.size() == 2);
    auto rebuilt = to_path(contours, FillRule::EVEN_ODD);
    REQUIRE(rebuilt.get_fill_rule() == FillRule::EVEN_ODD);
    REQUIRE(to_contours(rebuilt) == contours);
    auto& segments = rebuilt.get_segments();
    REQUIRE(segments[1].m_type == SegmentType::LINE);
    REQUIRE(segments[2].m_type == SegmentType::CUBIC);
    REQUIRE(segments[3].m_type == SegmentType::CUBIC);
    REQUIRE(segments[4].m_type == SegmentType::CLOSE);
    auto lone = Path();
    lone.move_to(Point(3, 4));
    REQUIRE(to_contours(lone) ==
      std::vector{Contour(std::vector{make_anchor(Point(3, 4))}, false)});
    REQUIRE(to_path(to_contours(lone), FillRule::NON_ZERO) == lone);
    auto circle = to_contours(make_circle(Point(0, 0), 10));
    REQUIRE(circle.size() == 1);
    REQUIRE(circle[0].m_is_closed);
    REQUIRE(circle[0].m_anchors.size() == 4);
    for(auto& anchor : circle[0].m_anchors) {
      REQUIRE(is_smooth(anchor));
    }
  }

  TEST_CASE("curved_closing_segment") {
    auto contour = make_curve();
    contour.m_is_closed = true;
    contour.m_anchors[1].m_out = Point(100, 40);
    contour.m_anchors[0].m_in = Point(0, 40);
    auto path = to_path(std::vector{contour}, FillRule::NON_ZERO);
    auto& segments = path.get_segments();
    REQUIRE(segments.size() == 4);
    REQUIRE(segments[2].m_type == SegmentType::CUBIC);
    REQUIRE(segments[2].m_points[2] == Point(0, 0));
    REQUIRE(segments[3].m_type == SegmentType::CLOSE);
    REQUIRE(to_contours(path) == std::vector{contour});
  }

  TEST_CASE("transform") {
    auto contour = transform(make_curve(), make_translation(5, 10));
    REQUIRE(contour.m_anchors[0] ==
      Anchor(Point(5, 10), Point(5, 10), Point(5, -30)));
    REQUIRE(contour.m_anchors[1].m_in == Point(105, -30));
  }

  TEST_CASE("segments") {
    auto contour = make_curve();
    REQUIRE(get_segment_count(contour) == 1);
    REQUIRE(evaluate(contour, 0, 0) == Point(0, 0));
    REQUIRE(evaluate(contour, 0, 0.5) == Point(50, -30));
    contour.m_is_closed = true;
    REQUIRE(get_segment_count(contour) == 2);
    REQUIRE(evaluate(contour, 1, 0.5) == Point(50, 0));
    REQUIRE(get_segment_count(Contour()) == 0);
  }

  TEST_CASE("nearest") {
    auto contour = make_curve();
    contour.m_is_closed = true;
    auto top = find_nearest(contour, Point(50, -40));
    REQUIRE(top);
    REQUIRE(top->m_segment == 0);
    REQUIRE(top->m_t == doctest::Approx(0.5).epsilon(1e-4));
    REQUIRE(top->m_point.m_y == doctest::Approx(-30));
    auto bottom = find_nearest(contour, Point(25, 3));
    REQUIRE(bottom);
    REQUIRE(bottom->m_segment == 1);
    REQUIRE(bottom->m_t == doctest::Approx(0.75));
    REQUIRE(bottom->m_point == Point(25, 0));
    REQUIRE(!find_nearest(Contour(), Point(0, 0)));
  }

  TEST_CASE("split") {
    auto contour = make_curve();
    auto original = contour;
    auto index = split_segment(contour, 0, 0.5);
    REQUIRE(index == 1);
    REQUIRE(contour.m_anchors.size() == 3);
    REQUIRE(contour.m_anchors[1].m_point == Point(50, -30));
    REQUIRE(is_smooth(contour.m_anchors[1]));
    for(auto t : {0.1, 0.3, 0.7}) {
      REQUIRE(is_near(evaluate(original, 0, t / 2),
        evaluate(contour, 0, t)));
      REQUIRE(is_near(evaluate(original, 0, 0.5 + t / 2),
        evaluate(contour, 1, t)));
    }
    contour.m_is_closed = true;
    auto closing = split_segment(contour, 2, 0.25);
    REQUIRE(closing == 3);
    REQUIRE(contour.m_anchors[3] == make_anchor(Point(75, 0)));
  }

  TEST_CASE("removal") {
    auto contour = make_curve();
    split_segment(contour, 0, 0.5);
    contour.m_is_closed = true;
    remove_anchor(contour, 1);
    auto expected = make_curve();
    expected.m_anchors[0].m_out = Point(0, -20);
    expected.m_anchors[1].m_in = Point(100, -20);
    expected.m_is_closed = true;
    REQUIRE(contour == expected);
  }

  TEST_CASE("smoothing") {
    auto contour = Contour();
    contour.m_anchors.push_back(make_anchor(Point(0, 0)));
    contour.m_anchors.push_back(make_anchor(Point(30, 30)));
    contour.m_anchors.push_back(make_anchor(Point(60, 0)));
    set_smooth(contour, 1, true);
    auto& middle = contour.m_anchors[1];
    REQUIRE(is_smooth(middle));
    REQUIRE(is_near(middle.m_in, Point(30 - std::hypot(30, 30) / 3, 30)));
    REQUIRE(is_near(middle.m_out, Point(30 + std::hypot(30, 30) / 3, 30)));
    set_smooth(contour, 0, true);
    REQUIRE(is_near(contour.m_anchors[0].m_out, Point(10, 10)));
    REQUIRE(is_smooth(contour.m_anchors[0]));
    set_smooth(contour, 1, false);
    REQUIRE(contour.m_anchors[1] == make_anchor(Point(30, 30)));
    auto lone = Contour(std::vector{make_anchor(Point(1, 2))}, false);
    set_smooth(lone, 0, true);
    REQUIRE(lone.m_anchors[0] == make_anchor(Point(1, 2)));
  }
}
