#include <cmath>
#include <numbers>
#include <doctest/doctest.h>
#include <QImage>
#include <QPainter>
#include "Rellu/Ui/GradientHandles.hpp"

using namespace Rellu;

namespace {
  GradientFill make_fill(GradientGeometry geometry) {
    auto fill = GradientFill();
    fill.m_geometry = geometry;
    fill.m_stops = {GradientStop(0, Color(1, 0, 0, 1)),
      GradientStop(1, Color(0, 0, 1, 1))};
    return fill;
  }

  GradientFrame make_frame() {
    return GradientFrame(Rect(0, 0, 100, 50), make_translation(10, 10));
  }

  Point map(const GradientFill& fill, const Rect& bounds, Point point) {
    return (make_translation(bounds.m_x, bounds.m_y) *
      make_scale(bounds.m_width, bounds.m_height) * fill.m_transform).map(
        point);
  }

  void require_near(Point actual, Point expected) {
    REQUIRE(actual.m_x == doctest::Approx(expected.m_x));
    REQUIRE(actual.m_y == doctest::Approx(expected.m_y));
  }
}

TEST_SUITE("GradientHandles") {
  TEST_CASE("editable") {
    REQUIRE(GradientHandles::is_editable(make_fill(LinearGradient())));
    REQUIRE(GradientHandles::is_editable(make_fill(AngularGradient())));
    REQUIRE(GradientHandles::is_editable(make_fill(RadialGradient())));
    REQUIRE(!GradientHandles::is_editable(make_fill(
      RadialGradient(Point(0.5, 0.5), 0.5, Point(0.2, 0.5)))));
    auto user = make_fill(LinearGradient());
    user.m_units = GradientUnits::USER_SPACE;
    REQUIRE(!GradientHandles::is_editable(user));
  }

  TEST_CASE("linear") {
    auto handles = GradientHandles();
    auto frame = make_frame();
    auto fill =
      make_fill(LinearGradient(Point(0, 0.5), Point(1, 0.5)));
    auto edit = handles.press(fill, frame, Point(11, 36));
    REQUIRE(edit.m_is_handled);
    REQUIRE(edit.m_stop == 0);
    REQUIRE(!edit.m_fill);
    edit = handles.move(fill, frame, Point(30, 20), {});
    auto& linear = std::get<LinearGradient>(edit.m_fill->m_geometry);
    require_near(linear.m_start, Point(0.2, 0.2));
    require_near(linear.m_end, Point(1, 0.5));
    edit = handles.move(fill, frame, Point(12, 61), {});
    require_near(std::get<LinearGradient>(edit.m_fill->m_geometry).m_start,
      Point(0, 1));
    handles.release();
    edit = handles.press(fill, frame, Point(60, 36));
    REQUIRE(edit.m_stop == 1);
    auto added = *edit.m_fill;
    REQUIRE(added.m_stops.size() == 3);
    REQUIRE(added.m_stops[1].m_offset == doctest::Approx(0.5));
    REQUIRE(added.m_stops[1].m_color.m_red == doctest::Approx(0.5));
    edit = handles.move(added, frame, Point(110, 40), {});
    REQUIRE(edit.m_stop == 2);
    REQUIRE(edit.m_fill->m_stops[2].m_offset == 1);
    handles.release();
    REQUIRE(!handles.press(fill, frame, Point(60, 70)).m_is_handled);
  }

  TEST_CASE("radial") {
    auto handles = GradientHandles();
    auto frame = make_frame();
    auto& bounds = frame.m_bounds;
    auto fill = make_fill(RadialGradient());
    auto edit = handles.press(fill, frame, Point(110, 35));
    REQUIRE(edit.m_stop == 1);
    edit = handles.move(fill, frame, Point(60, 85), {});
    auto rotated = *edit.m_fill;
    auto& radial = std::get<RadialGradient>(rotated.m_geometry);
    require_near(map(rotated, bounds, radial.m_center), Point(50, 25));
    require_near(map(rotated, bounds,
      radial.m_center + Point(radial.m_radius, 0)), Point(50, 75));
    require_near(map(rotated, bounds,
      radial.m_center + Point(0, radial.m_radius)), Point(25, 25));
    handles.release();
    edit = handles.press(fill, frame, Point(110, 60));
    REQUIRE(edit.m_is_handled);
    edit = handles.move(fill, frame, Point(130, 70), {});
    auto resized = *edit.m_fill;
    auto& circle = std::get<RadialGradient>(resized.m_geometry);
    require_near(map(resized, bounds,
      circle.m_center + Point(circle.m_radius, 0)), Point(120, 25));
    require_near(map(resized, bounds,
      circle.m_center + Point(0, circle.m_radius)), Point(50, 60));
    edit = handles.move(fill, frame, Point(130, 60), Qt::ShiftModifier);
    auto kept = *edit.m_fill;
    auto& proportional = std::get<RadialGradient>(kept.m_geometry);
    require_near(map(kept, bounds,
      proportional.m_center + Point(proportional.m_radius, 0)),
      Point(100, 25));
    handles.release();
    edit = handles.press(fill, frame, Point(60, 35));
    REQUIRE(edit.m_stop == 0);
    edit = handles.move(fill, frame, Point(40, 25), {});
    auto& moved = std::get<RadialGradient>(edit.m_fill->m_geometry);
    require_near(moved.m_center, Point(0.3, 0.3));
    REQUIRE(moved.m_focus == moved.m_center);
    REQUIRE(edit.m_fill->m_transform.is_identity());
  }

  TEST_CASE("angular") {
    auto handles = GradientHandles();
    auto frame = make_frame();
    auto fill = make_fill(AngularGradient());
    auto edit = handles.press(fill, frame, Point(60, 35));
    REQUIRE(edit.m_is_handled);
    edit = handles.move(fill, frame, Point(70, 45), {});
    require_near(std::get<AngularGradient>(edit.m_fill->m_geometry).m_center,
      Point(0.6, 0.7));
    handles.release();
    edit = handles.press(fill, frame, Point(110, 35));
    REQUIRE(edit.m_stop == 0);
    auto image = QImage(200, 120, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    auto painter = QPainter(&image);
    handles.paint(painter, fill, frame, 0);
    painter.end();
    REQUIRE(image.pixelColor(110, 35).alpha() != 0);
  }
}
