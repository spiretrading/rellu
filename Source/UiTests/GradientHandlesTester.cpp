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

  GradientEdit press_key(const GradientFill& fill, int stop, int key,
      Qt::KeyboardModifiers modifiers = {}, double zoom = 1) {
    return GradientHandles::press_key(
      fill, Rect(0, 0, 100, 50), zoom, stop, key, modifiers);
  }

  std::vector<double> get_offsets(const GradientFill& fill) {
    auto offsets = std::vector<double>();
    for(auto& stop : fill.m_stops) {
      offsets.push_back(stop.m_offset);
    }
    return offsets;
  }

  void require_offsets(
      const GradientFill& fill, const std::vector<double>& expected) {
    auto offsets = get_offsets(fill);
    REQUIRE(offsets.size() == expected.size());
    for(auto i = std::size_t(0); i != offsets.size(); ++i) {
      REQUIRE(offsets[i] == doctest::Approx(expected[i]));
    }
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

  TEST_CASE("unhandled_keys") {
    for(auto& geometry : {GradientGeometry(LinearGradient()),
        GradientGeometry(RadialGradient()),
        GradientGeometry(AngularGradient())}) {
      auto fill = make_fill(geometry);
      fill.m_stops.insert(
        fill.m_stops.begin() + 1, GradientStop(0.5, Color(0, 1, 0, 1)));
      REQUIRE(!press_key(fill, 0, Qt::Key_Tab).m_is_handled);
      REQUIRE(!press_key(fill, 2, Qt::Key_Tab).m_is_handled);
      REQUIRE(!press_key(
        fill, 0, Qt::Key_Backtab, Qt::ShiftModifier).m_is_handled);
      REQUIRE(!press_key(fill, 0, Qt::Key_A).m_is_handled);
    }
    REQUIRE(!press_key(make_fill(RadialGradient(Point(0.5, 0.5), 0.5,
      Point(0.2, 0.5))), 0, Qt::Key_Delete).m_is_handled);
  }

  TEST_CASE("stop_deletion") {
    auto red = Color(1, 0, 0, 1);
    auto green = Color(0, 1, 0, 1);
    auto blue = Color(0, 0, 1, 1);
    for(auto& geometry : {GradientGeometry(LinearGradient()),
        GradientGeometry(RadialGradient()),
        GradientGeometry(AngularGradient())}) {
      auto fill = make_fill(geometry);
      for(auto key : {Qt::Key_Delete, Qt::Key_Backspace}) {
        for(auto stop : {0, 1}) {
          auto edit = press_key(fill, stop, key);
          REQUIRE(edit.m_is_handled);
          REQUIRE(!edit.m_fill);
          REQUIRE(!edit.m_stop);
        }
      }
      fill.m_stops.insert(fill.m_stops.begin() + 1, GradientStop(0.25, green));
      fill.m_stops.insert(fill.m_stops.begin() + 2, GradientStop(0.75, blue));
      auto edit = press_key(fill, 2, Qt::Key_Backspace);
      REQUIRE(edit.m_stop == 1);
      REQUIRE(edit.m_fill->m_geometry == geometry);
      REQUIRE(edit.m_fill->m_stops == std::vector({GradientStop(0, red),
        GradientStop(0.25, green), GradientStop(1, blue)}));
    }
    auto fill = make_fill(LinearGradient());
    fill.m_stops = {GradientStop(0.2, red), GradientStop(0.8, blue)};
    auto edit = press_key(fill, 0, Qt::Key_Delete);
    REQUIRE(edit.m_stop == 0);
    REQUIRE(edit.m_fill->m_stops == std::vector({GradientStop(0, red),
      GradientStop(0.8, blue), GradientStop(1, blue)}));
  }

  TEST_CASE("linear_nudge") {
    auto fill = make_fill(LinearGradient(Point(0, 0.5), Point(1, 0.5)));
    fill.m_stops.insert(
      fill.m_stops.begin() + 1, GradientStop(0.5, Color(0, 1, 0, 1)));
    auto edit = press_key(fill, 1, Qt::Key_Right);
    REQUIRE(edit.m_is_handled);
    REQUIRE(edit.m_stop == 1);
    require_offsets(*edit.m_fill, {0, 0.51, 1});
    require_offsets(
      *press_key(fill, 1, Qt::Key_Left, Qt::ShiftModifier).m_fill,
      {0, 0.4, 1});
    require_offsets(
      *press_key(fill, 1, Qt::Key_Right, {}, 2).m_fill, {0, 0.505, 1});
    require_offsets(*press_key(fill, 1, Qt::Key_Up).m_fill, {0, 0.5, 1});
    REQUIRE(!press_key(
      fill, 1, Qt::Key_Left, Qt::ShiftModifier | Qt::ControlModifier).
        m_is_handled);
    fill.m_stops[1].m_offset = 0.95;
    edit = press_key(fill, 1, Qt::Key_Right, Qt::ShiftModifier);
    REQUIRE(edit.m_stop == 1);
    require_offsets(*edit.m_fill, {0, 1, 1});
    REQUIRE(edit.m_fill->m_stops[1].m_color == Color(0, 1, 0, 1));
    fill.m_stops[1].m_offset = 0.5;
    fill.m_stops.insert(
      fill.m_stops.begin() + 2, GradientStop(0.55, Color(1, 1, 1, 1)));
    edit = press_key(fill, 1, Qt::Key_Right, Qt::ShiftModifier);
    REQUIRE(edit.m_stop == 2);
    require_offsets(*edit.m_fill, {0, 0.55, 0.6, 1});
    REQUIRE(edit.m_fill->m_stops[2].m_color == Color(0, 1, 0, 1));
    edit = press_key(fill, 0, Qt::Key_Right);
    REQUIRE(!edit.m_stop);
    REQUIRE(edit.m_fill->m_stops == fill.m_stops);
    auto& start = std::get<LinearGradient>(edit.m_fill->m_geometry);
    require_near(start.m_start, Point(0.01, 0.5));
    require_near(start.m_end, Point(1, 0.5));
    edit = press_key(fill, 3, Qt::Key_Down);
    require_near(std::get<LinearGradient>(edit.m_fill->m_geometry).m_end,
      Point(1, 0.52));
    auto inner = make_fill(LinearGradient(Point(0, 0.5), Point(1, 0.5)));
    inner.m_stops = {GradientStop(0.2, Color(1, 0, 0, 1)),
      GradientStop(0.8, Color(0, 0, 1, 1))};
    edit = press_key(inner, 0, Qt::Key_Right);
    REQUIRE(edit.m_stop == 1);
    require_offsets(*edit.m_fill, {0, 0.21, 0.8, 1});
  }

  TEST_CASE("nudge") {
    auto fill = make_fill(LinearGradient(Point(0, 0.5), Point(1, 0.5)));
    fill.m_stops.insert(
      fill.m_stops.begin() + 1, GradientStop(0.5, Color(0, 1, 0, 1)));
    auto bounds = Rect(0, 0, 100, 50);
    auto edit = GradientHandles::nudge(fill, bounds, 1, 1, Point(4, 0));
    REQUIRE(edit.m_is_handled);
    REQUIRE(edit.m_stop == 1);
    require_offsets(*edit.m_fill, {0, 0.54, 1});
    require_offsets(
      *GradientHandles::nudge(fill, bounds, 2, 1, Point(-6, 0)).m_fill,
      {0, 0.47, 1});
    REQUIRE(!GradientHandles::nudge(make_fill(RadialGradient(Point(0.5, 0.5),
      0.5, Point(0.2, 0.5))), bounds, 1, 0, Point(1, 0)).m_is_handled);
  }

  TEST_CASE("radial_nudge") {
    auto bounds = Rect(0, 0, 100, 50);
    auto fill = make_fill(RadialGradient());
    fill.m_stops.insert(
      fill.m_stops.begin() + 1, GradientStop(0.5, Color(0, 1, 0, 1)));
    auto edit = press_key(fill, 1, Qt::Key_Right);
    REQUIRE(edit.m_stop == 1);
    require_offsets(*edit.m_fill, {0, 0.52, 1});
    require_offsets(*press_key(fill, 1, Qt::Key_Down).m_fill, {0, 0.5, 1});
    edit = press_key(fill, 0, Qt::Key_Right);
    auto& moved = std::get<RadialGradient>(edit.m_fill->m_geometry);
    require_near(moved.m_center, Point(0.51, 0.5));
    REQUIRE(moved.m_focus == moved.m_center);
    REQUIRE(edit.m_fill->m_stops == fill.m_stops);
    edit = press_key(fill, 2, Qt::Key_Down);
    auto turned = *edit.m_fill;
    auto& circle = std::get<RadialGradient>(turned.m_geometry);
    require_near(map(turned, bounds, circle.m_center), Point(50, 25));
    require_near(map(turned, bounds,
      circle.m_center + Point(circle.m_radius, 0)), Point(100, 26));
  }

  TEST_CASE("angular_nudge") {
    auto fill = make_fill(AngularGradient());
    fill.m_stops.insert(
      fill.m_stops.begin() + 1, GradientStop(0.25, Color(0, 1, 0, 1)));
    auto edit = press_key(fill, 1, Qt::Key_Left);
    REQUIRE(edit.m_stop == 1);
    require_offsets(*edit.m_fill,
      {0, std::atan2(0.5, -0.01) / (2 * std::numbers::pi), 1});
    auto turn = std::atan2(0.02, 0.5) / (2 * std::numbers::pi);
    edit = press_key(fill, 0, Qt::Key_Down);
    REQUIRE(!edit.m_stop);
    REQUIRE(std::get<AngularGradient>(edit.m_fill->m_geometry).m_rotation ==
      doctest::Approx(360 * turn));
    require_offsets(*edit.m_fill, {0, 0.25 - turn, 1});
    edit = press_key(fill, 2, Qt::Key_Up);
    REQUIRE(std::get<AngularGradient>(edit.m_fill->m_geometry).m_rotation ==
      doctest::Approx(360 * (1 - turn)));
    require_offsets(*edit.m_fill, {0, 0.25 + turn, 1});
  }
}
