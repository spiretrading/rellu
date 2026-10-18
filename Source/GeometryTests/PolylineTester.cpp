#include <numbers>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Polyline.hpp"

using namespace Rellu;

namespace {
  constexpr auto KAPPA = 4 * (std::numbers::sqrt2 - 1) / 3;

  Path make_quarter_circle(double radius) {
    auto path = Path();
    path.move_to(Point(radius, 0));
    path.cubic_to(Point(radius, KAPPA * radius),
      Point(KAPPA * radius, radius), Point(0, radius));
    return path;
  }
}

TEST_SUITE("Polyline") {
  TEST_CASE("open_lines") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.line_to(Point(5, 5));
    path.line_to(Point(5, 0));
    REQUIRE(flatten(path, 0.1) == std::vector{
      Polyline({Point(0, 0), Point(5, 5), Point(5, 0)}, false)});
  }

  TEST_CASE("closed_lines") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.line_to(Point(10, 0));
    path.line_to(Point(10, 10));
    path.close();
    path.move_to(Point(20, 0));
    path.line_to(Point(30, 0));
    path.line_to(Point(30, 10));
    path.line_to(Point(20, 0));
    path.close();
    REQUIRE(flatten(path, 0.1) == std::vector{
      Polyline({Point(0, 0), Point(10, 0), Point(10, 10)}, true),
      Polyline({Point(20, 0), Point(30, 0), Point(30, 10)}, true)});
  }

  TEST_CASE("lone_move") {
    auto path = Path();
    path.move_to(Point(1, 1));
    path.line_to(Point(2, 2));
    path.move_to(Point(5, 5));
    REQUIRE(flatten(path, 0.1) == std::vector{
      Polyline({Point(1, 1), Point(2, 2)}, false)});
    REQUIRE(flatten(Path(), 0.1).empty());
  }

  TEST_CASE("closed_point") {
    auto path = Path();
    path.move_to(Point(3, 4));
    path.close();
    REQUIRE(flatten(path, 0.1) == std::vector{
      Polyline({Point(3, 4)}, true)});
  }

  TEST_CASE("straight_cubic") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.cubic_to(Point(10, 0), Point(20, 0), Point(30, 0));
    REQUIRE(flatten(path, 0.1) == std::vector{
      Polyline({Point(0, 0), Point(30, 0)}, false)});
  }

  TEST_CASE("cubic_tolerance") {
    auto radius = 100.0;
    auto approximation_error = 0.03;
    for(auto tolerance : {1.0, 0.25, 0.01}) {
      auto polylines = flatten(make_quarter_circle(radius), tolerance);
      REQUIRE(polylines.size() == 1);
      auto& points = polylines.front().m_points;
      REQUIRE(points.front() == Point(radius, 0));
      REQUIRE(points.back() == Point(0, radius));
      for(auto& point : points) {
        REQUIRE(length(point) < radius + approximation_error);
      }
      for(auto i = std::size_t(1); i != points.size(); ++i) {
        auto middle = interpolate(points[i - 1], points[i], 0.5);
        REQUIRE(length(middle) > radius - tolerance - approximation_error);
      }
    }
  }

  TEST_CASE("subdivision_count") {
    auto coarse = flatten(make_quarter_circle(100), 1);
    auto fine = flatten(make_quarter_circle(100), 0.01);
    REQUIRE(coarse.front().m_points.size() > 2);
    REQUIRE(fine.front().m_points.size() > coarse.front().m_points.size());
  }

  TEST_CASE("quadratic_tolerance") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.quadratic_to(Point(50, 100), Point(100, 0));
    auto tolerance = 0.1;
    auto polylines = flatten(path, tolerance);
    REQUIRE(polylines.size() == 1);
    auto& points = polylines.front().m_points;
    REQUIRE(points.front() == Point(0, 0));
    REQUIRE(points.back() == Point(100, 0));
    for(auto i = std::size_t(1); i != points.size(); ++i) {
      auto middle = interpolate(points[i - 1], points[i], 0.5);
      auto x = middle.m_x / 100;
      auto curve = 100 * 2 * x * (1 - x);
      REQUIRE(curve - middle.m_y < tolerance);
      REQUIRE(curve - middle.m_y >= -1e-9);
    }
  }
}
