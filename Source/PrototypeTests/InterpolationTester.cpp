#include <cmath>
#include <numbers>
#include <doctest/doctest.h>
#include "Rellu/Prototype/Interpolation.hpp"

using namespace Rellu;

namespace {
  void require_near(const Matrix& actual, const Matrix& expected) {
    REQUIRE(actual.m_a == doctest::Approx(expected.m_a));
    REQUIRE(actual.m_b == doctest::Approx(expected.m_b));
    REQUIRE(actual.m_c == doctest::Approx(expected.m_c));
    REQUIRE(actual.m_d == doctest::Approx(expected.m_d));
    REQUIRE(actual.m_tx == doctest::Approx(expected.m_tx));
    REQUIRE(actual.m_ty == doctest::Approx(expected.m_ty));
  }

  Shape make_shape(ShapeGeometry geometry, const Color& color) {
    auto shape = Shape();
    shape.m_geometry = std::move(geometry);
    shape.m_style.m_fill = SolidFill(color);
    return shape;
  }
}

TEST_SUITE("Interpolation") {
  TEST_CASE("numbers") {
    REQUIRE(interpolate(10.0, 20.0, 0.25) == doctest::Approx(12.5));
    REQUIRE(interpolate(10.0, 20.0, 1.5) == doctest::Approx(25));
  }

  TEST_CASE("colors") {
    auto color = interpolate(Color(0, 0, 0, 1), Color(1, 0.5, 0, 0), 0.5);
    REQUIRE(color.m_red == doctest::Approx(0.5));
    REQUIRE(color.m_green == doctest::Approx(0.25));
    REQUIRE(color.m_alpha == doctest::Approx(0.5));
    REQUIRE(interpolate(Color(0, 0, 0, 1), Color(1, 1, 1, 1), 1.5) ==
      Color(1, 1, 1, 1));
  }

  TEST_CASE("transforms") {
    require_near(interpolate(make_translation(0, 0),
      make_translation(100, 50), 0.5), make_translation(50, 25));
    require_near(interpolate(Matrix(), make_rotation(std::numbers::pi / 2),
      0.5), make_rotation(std::numbers::pi / 4));
    require_near(interpolate(make_rotation(-3), make_rotation(3), 0.5),
      make_rotation(std::numbers::pi));
    require_near(interpolate(make_scale(1, 1), make_scale(3, 2), 0.5),
      make_scale(2, 1.5));
    auto start = make_translation(10, 0) * make_rotation(0.2);
    auto end = make_translation(30, 20) * make_rotation(0.6) *
      make_scale(2, 2);
    require_near(interpolate(start, end, 0), start);
    require_near(interpolate(start, end, 1), end);
    require_near(interpolate(start, end, 0.5), make_translation(20, 10) *
      make_rotation(0.4) * make_scale(1.5, 1.5));
  }

  TEST_CASE("fills") {
    auto fill = interpolate(Fill(SolidFill(Color(0, 0, 0, 1))),
      Fill(SolidFill(Color(1, 1, 1, 1))), 0.25);
    REQUIRE(fill == Fill(SolidFill(Color(0.25, 0.25, 0.25, 1))));
    auto start = GradientFill();
    start.m_stops = {GradientStop(0, Color(0, 0, 0, 1)),
      GradientStop(1, Color(1, 1, 1, 1))};
    auto end = start;
    end.m_stops[1].m_color = Color(0, 0, 0, 1);
    end.m_stops[0].m_offset = 0.5;
    auto gradient = interpolate(Fill(start), Fill(end), 0.5);
    REQUIRE(gradient);
    auto& stops = std::get<GradientFill>(*gradient).m_stops;
    REQUIRE(stops[0].m_offset == doctest::Approx(0.25));
    REQUIRE(stops[1].m_color.m_red == doctest::Approx(0.5));
    end.m_stops.pop_back();
    REQUIRE(!interpolate(Fill(start), Fill(end), 0.5));
    REQUIRE(!interpolate(Fill(NoFill()), Fill(start), 0.5));
  }

  TEST_CASE("paths") {
    auto start = Path();
    start.move_to(Point(0, 0));
    start.line_to(Point(10, 0));
    start.cubic_to(Point(10, 5), Point(5, 10), Point(0, 10));
    start.close();
    auto end = Path();
    end.move_to(Point(10, 10));
    end.line_to(Point(30, 10));
    end.cubic_to(Point(30, 15), Point(15, 30), Point(10, 30));
    end.close();
    auto morph = interpolate(start, end, 0.5);
    REQUIRE(morph);
    auto& segments = morph->get_segments();
    REQUIRE(segments.size() == end.get_segments().size());
    REQUIRE(segments[1].m_points[0] == Point(20, 5));
    REQUIRE(segments[2].m_points[2] == Point(5, 20));
    auto other = Path();
    other.move_to(Point(0, 0));
    other.line_to(Point(1, 1));
    REQUIRE(!interpolate(start, other, 0.5));
  }

  TEST_CASE("payloads") {
    auto start = Payload(Box<Shape>(make_shape(RectangleShape(
      Rect(0, 0, 100, 50), CornerRadii(0, 0, 0, 0)), Color(1, 0, 0, 1))));
    auto end = Payload(Box<Shape>(make_shape(RectangleShape(
      Rect(0, 0, 200, 150), CornerRadii(10, 10, 10, 10)), Color(0, 0, 1, 1))));
    auto middle = interpolate(start, end, 0.5);
    auto& shape = **std::get_if<Box<Shape>>(&middle);
    auto& rectangle = std::get<RectangleShape>(shape.m_geometry);
    REQUIRE(rectangle.m_bounds == Rect(0, 0, 150, 100));
    REQUIRE(rectangle.m_radii == CornerRadii(5, 5, 5, 5));
    REQUIRE(std::get<SolidFill>(shape.m_style.m_fill).m_color ==
      Color(0.5, 0, 0.5, 1));
    auto ellipse = Payload(Box<Shape>(make_shape(
      EllipseShape(Point(5, 5), 5, 5), Color(0, 0, 0, 1))));
    auto mixed = interpolate(start, ellipse, 0.5);
    REQUIRE(std::holds_alternative<EllipseShape>(
      (*std::get_if<Box<Shape>>(&mixed))->m_geometry));
    auto first = Artboard();
    first.m_background = SolidFill(Color(0, 0, 0, 1));
    auto second = Artboard();
    second.m_frame = Rect(500, 0, 10, 10);
    second.m_background = SolidFill(Color(1, 1, 1, 1));
    auto board = interpolate(
      Payload(Box<Artboard>(first)), Payload(Box<Artboard>(second)), 0.5);
    auto& artboard = **std::get_if<Box<Artboard>>(&board);
    REQUIRE(artboard.m_frame == second.m_frame);
    REQUIRE(std::get<SolidFill>(artboard.m_background).m_color ==
      Color(0.5, 0.5, 0.5, 1));
  }

  TEST_CASE("headers") {
    auto start = NodeHeader();
    start.m_name = "Start";
    start.m_opacity = 0;
    auto end = NodeHeader();
    end.m_name = "End";
    end.m_transform = make_translation(10, 0);
    auto header = interpolate(start, end, 0.5);
    REQUIRE(header.m_name == "End");
    REQUIRE(header.m_opacity == doctest::Approx(0.5));
    REQUIRE(header.m_transform.m_tx == doctest::Approx(5));
    REQUIRE(interpolate(start, end, 2).m_opacity == 1);
  }
}
