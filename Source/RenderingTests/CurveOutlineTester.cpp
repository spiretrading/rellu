#include <cmath>
#include <numbers>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "CurveOutline.hpp"

using namespace Rellu;

namespace {
  using Verb = CurveOutline::Verb;
  using Vertex = CurveOutline::Vertex;

  CurveOutline stroke(const Path& line, double width,
      StrokeCap cap = StrokeCap::BUTT, StrokeJoin join = StrokeJoin::MITER,
      const Matrix& matrix = Matrix()) {
    return stroke_outline(line, matrix, width, cap, join, 4);
  }

  bool is_near(Vertex vertex, double x, double y) {
    return std::abs(vertex.m_x - x) < 1e-5 && std::abs(vertex.m_y - y) < 1e-5;
  }

  int count(const CurveOutline& outline, Verb verb) {
    auto total = 0;
    for(auto entry : outline.m_verbs) {
      if(entry == verb) {
        ++total;
      }
    }
    return total;
  }

  std::vector<Vertex> get_ends(const CurveOutline& outline) {
    auto ends = std::vector<Vertex>();
    auto index = std::size_t(0);
    for(auto verb : outline.m_verbs) {
      if(verb == Verb::MOVE || verb == Verb::LINE) {
        ends.push_back(outline.m_points[index]);
        index += 1;
      } else if(verb == Verb::QUADRATIC || verb == Verb::CONIC) {
        ends.push_back(outline.m_points[index + 1]);
        index += 2;
      } else if(verb == Verb::CUBIC) {
        ends.push_back(outline.m_points[index + 2]);
        index += 3;
      }
    }
    return ends;
  }
}

TEST_SUITE("CurveOutline") {
  TEST_CASE("rectangle") {
    auto outline = stroke(make_rectangle(Rect(1, 1, 4, 4)), 2);
    REQUIRE(outline.m_verbs == std::vector{Verb::MOVE, Verb::LINE, Verb::LINE,
      Verb::LINE, Verb::CLOSE, Verb::MOVE, Verb::LINE, Verb::LINE, Verb::LINE,
      Verb::CLOSE});
    auto& points = outline.m_points;
    REQUIRE(is_near(points[0], 0, 0));
    REQUIRE(is_near(points[1], 6, 0));
    REQUIRE(is_near(points[2], 6, 6));
    REQUIRE(is_near(points[3], 0, 6));
    REQUIRE(is_near(points[4], 2, 2));
    REQUIRE(is_near(points[5], 2, 4));
    REQUIRE(is_near(points[6], 4, 4));
    REQUIRE(is_near(points[7], 4, 2));
    auto filled = stroke(make_rectangle(Rect(1, 1, 4, 4)), 4);
    REQUIRE(filled.m_verbs.size() == 5);
    auto round = stroke(
      make_rectangle(Rect(1, 1, 4, 4)), 2, StrokeCap::BUTT, StrokeJoin::ROUND);
    REQUIRE(count(round, Verb::CONIC) == 4);
    REQUIRE(is_near(round.m_points[0], 0, 5));
  }

  TEST_CASE("line") {
    auto outline = stroke(make_line(Point(0, 0), Point(10, 0)), 2);
    REQUIRE(outline.m_verbs == std::vector{Verb::MOVE, Verb::LINE, Verb::LINE,
      Verb::LINE, Verb::LINE, Verb::CLOSE});
    auto& points = outline.m_points;
    REQUIRE(is_near(points[0], 0, -1));
    REQUIRE(is_near(points[1], 10, -1));
    REQUIRE(is_near(points[2], 10, 1));
    REQUIRE(is_near(points[3], 0, 1));
    REQUIRE(is_near(points[4], 0, -1));
  }

  TEST_CASE("caps") {
    auto line = make_line(Point(0, 0), Point(10, 0));
    auto round = stroke(line, 2, StrokeCap::ROUND);
    REQUIRE(count(round, Verb::CONIC) == 4);
    for(auto weight : round.m_weights) {
      REQUIRE(weight == doctest::Approx(std::numbers::sqrt2 / 2));
    }
    auto square = stroke(line, 2, StrokeCap::SQUARE);
    auto ends = get_ends(square);
    REQUIRE(is_near(ends[0], 0, -1));
    REQUIRE(is_near(ends[1], 11, -1));
    REQUIRE(is_near(ends[2], 11, 1));
  }

  TEST_CASE("circle") {
    auto outline = stroke(make_circle(Point(10, 10), 5), 2);
    REQUIRE(count(outline, Verb::MOVE) == 2);
    REQUIRE(count(outline, Verb::QUADRATIC) > 0);
    REQUIRE(count(outline, Verb::LINE) == 0);
    for(auto end : get_ends(outline)) {
      auto distance = std::hypot(end.m_x - 10, end.m_y - 10);
      REQUIRE((std::abs(distance - 6) < 1e-4 ||
        std::abs(distance - 4) < 1e-4));
    }
  }

  TEST_CASE("resolution") {
    auto circle = make_circle(Point(10, 10), 5);
    auto coarse = stroke(circle, 2);
    auto fine =
      stroke(circle, 2, StrokeCap::BUTT, StrokeJoin::MITER, make_scale(8, 8));
    REQUIRE(count(fine, Verb::QUADRATIC) > count(coarse, Verb::QUADRATIC));
    for(auto end : get_ends(fine)) {
      auto distance = std::hypot(end.m_x - 80, end.m_y - 80);
      REQUIRE((std::abs(distance - 48) < 1e-3 ||
        std::abs(distance - 32) < 1e-3));
    }
  }

  TEST_CASE("joins") {
    auto corner = Path();
    corner.move_to(Point(0, 0));
    corner.line_to(Point(10, 0));
    corner.line_to(Point(10, 10));
    auto miter = get_ends(stroke(corner, 2));
    REQUIRE(is_near(miter[1], 11, -1));
    auto bevel = stroke(corner, 2, StrokeCap::BUTT, StrokeJoin::BEVEL);
    auto bevel_ends = get_ends(bevel);
    REQUIRE(is_near(bevel_ends[1], 10, -1));
    REQUIRE(is_near(bevel_ends[2], 11, 0));
    auto round = stroke(corner, 2, StrokeCap::BUTT, StrokeJoin::ROUND);
    REQUIRE(count(round, Verb::CONIC) == 1);
  }
}
