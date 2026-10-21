#include <cmath>
#include <cstdlib>
#include <numbers>
#include <doctest/doctest.h>
#include "Rellu/Tools/CanvasCursors.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  PointerEvent make_event(
      Point position, Qt::MouseButton button, Qt::MouseButtons buttons) {
    return PointerEvent(position, button, buttons, {});
  }

  int get_alpha(const QImage& image, QPointF point) {
    auto ratio = image.devicePixelRatio();
    return qAlpha(image.pixel(static_cast<int>(point.x() * ratio),
      static_cast<int>(point.y() * ratio)));
  }
}

TEST_SUITE("CanvasCursors") {
  TEST_CASE("hot_spot") {
    struct Expectation {
      CursorKind m_kind;
      QPoint m_hot_spot;
      QPoint m_xd;
    };
    auto expectations = {
      Expectation(CursorKind::ARROW, QPoint(1, 1), QPoint(1, 1)),
      Expectation(CursorKind::RESIZE, QPoint(9, 9), QPoint(14, 14)),
      Expectation(CursorKind::ROTATE, QPoint(7, 7), QPoint(11, 11)),
      Expectation(CursorKind::RECTANGLE, QPoint(9, 9), QPoint(14, 14)),
      Expectation(CursorKind::ELLIPSE, QPoint(9, 9), QPoint(14, 14)),
      Expectation(CursorKind::POLYGON, QPoint(9, 9), QPoint(13, 13)),
      Expectation(CursorKind::LINE, QPoint(9, 9), QPoint(14, 14)),
      Expectation(CursorKind::ARTBOARD, QPoint(8, 8), QPoint(12, 12)),
      Expectation(CursorKind::PEN, QPoint(1, 1), QPoint(1, 1)),
      Expectation(CursorKind::TEXT, QPoint(4, 10), QPoint(6, 15)),
      Expectation(CursorKind::ZOOM_IN, QPoint(8, 8), QPoint(12, 12)),
      Expectation(CursorKind::ZOOM_OUT, QPoint(8, 8), QPoint(12, 12)),
      Expectation(CursorKind::OPEN_HAND, QPoint(11, 11), QPoint(17, 17)),
      Expectation(CursorKind::CLOSED_HAND, QPoint(11, 11), QPoint(16, 16)),
      Expectation(
        CursorKind::HORIZONTAL_GUIDE, QPoint(7, 8), QPoint(11, 12)),
      Expectation(CursorKind::VERTICAL_GUIDE, QPoint(8, 7), QPoint(12, 11))};
    for(const auto& expectation : expectations) {
      auto hot_spot = get_hot_spot(ToolCursor(expectation.m_kind));
      REQUIRE(hot_spot == expectation.m_hot_spot);
      REQUIRE(std::abs(qRound(hot_spot.x() * 1.5) - expectation.m_xd.x()) <=
        1);
      REQUIRE(std::abs(qRound(hot_spot.y() * 1.5) - expectation.m_xd.y()) <=
        1);
    }
  }

  TEST_CASE("orientation") {
    auto eighth = std::numbers::pi / 4;
    REQUIRE(get_orientation(ToolCursor(CursorKind::RESIZE, 0)) == 0);
    REQUIRE(get_orientation(ToolCursor(CursorKind::RESIZE, eighth)) == 1);
    REQUIRE(get_orientation(ToolCursor(CursorKind::RESIZE, -eighth)) == 3);
    REQUIRE(get_orientation(ToolCursor(CursorKind::RESIZE, 2.4 * eighth)) ==
      2);
    REQUIRE(get_orientation(ToolCursor(CursorKind::RESIZE, std::numbers::pi)) ==
      0);
    REQUIRE(get_orientation(ToolCursor(CursorKind::ROTATE, 0)) == 0);
    REQUIRE(get_orientation(ToolCursor(CursorKind::ROTATE, -3 * eighth)) == 5);
    REQUIRE(get_orientation(ToolCursor(CursorKind::ROTATE, 3 * eighth)) == 3);
    REQUIRE(get_orientation(ToolCursor(CursorKind::ROTATE, -2 * eighth)) ==
      6);
    REQUIRE(get_orientation(ToolCursor(CursorKind::TEXT, eighth)) == 0);
  }

  TEST_CASE("draw_cursor") {
    for(auto ratio : {1.0, 1.5, 2.0}) {
      auto image = draw_cursor(ToolCursor(), ratio);
      REQUIRE(image.width() == static_cast<int>(CANVAS_CURSOR_SIZE * ratio));
      REQUIRE(image.height() == image.width());
      REQUIRE(image.devicePixelRatio() == ratio);
      REQUIRE(get_alpha(image, QPointF(1.5, 1.5)) > 0);
      REQUIRE(get_alpha(image, QPointF(30, 30)) == 0);
    }
    auto pen = draw_cursor(ToolCursor(CursorKind::PEN), 1.5);
    REQUIRE(get_alpha(pen, QPointF(1.2, 1.5)) > 0);
    auto resize = draw_cursor(ToolCursor(CursorKind::RESIZE, 0), 1.5);
    REQUIRE(get_alpha(resize, QPointF(9.5, 9.5)) > 0);
    REQUIRE(get_alpha(resize, QPointF(1, 9.5)) > 0);
    REQUIRE(get_alpha(resize, QPointF(9.5, 1)) == 0);
    auto vertical = draw_cursor(
      ToolCursor(CursorKind::RESIZE, std::numbers::pi / 2), 1.5);
    REQUIRE(get_alpha(vertical, QPointF(9.5, 1)) > 0);
    REQUIRE(get_alpha(vertical, QPointF(1, 9.5)) == 0);
    auto rotate = draw_cursor(ToolCursor(CursorKind::ROTATE, 0), 1.5);
    REQUIRE(rotate != draw_cursor(
      ToolCursor(CursorKind::ROTATE, std::numbers::pi), 1.5));
    auto guide = draw_cursor(ToolCursor(CursorKind::HORIZONTAL_GUIDE), 1);
    REQUIRE(get_alpha(guide, QPointF(7.5, 1)) > 0);
    REQUIRE(get_alpha(guide, QPointF(1, 8.5)) > 0);
  }

  TEST_CASE("tool_cursor") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto rectangle = transaction.append(NodeId(),
      make_header("Rectangle", make_translation(300, 300)),
      make_rectangle_shape(200, 40));
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(rectangle);
    auto hover = [&] (Point position) {
      tools.move(make_event(position, Qt::NoButton, Qt::NoButton));
      return tools.get_cursor();
    };
    REQUIRE(hover(Point(600, 600)) == ToolCursor());
    auto corner = hover(Point(300, 300));
    REQUIRE(corner.m_kind == CursorKind::RESIZE);
    REQUIRE(get_orientation(corner) == 1);
    auto edge = hover(Point(400, 301));
    REQUIRE(edge.m_kind == CursorKind::RESIZE);
    REQUIRE(get_orientation(edge) == 2);
    auto side = hover(Point(500, 320));
    REQUIRE(side.m_kind == CursorKind::RESIZE);
    REQUIRE(get_orientation(side) == 0);
    REQUIRE(hover(Point(400, 291)) == ToolCursor());
    auto rotate = hover(Point(294, 294));
    REQUIRE(rotate.m_kind == CursorKind::ROTATE);
    REQUIRE(get_orientation(rotate) == 5);
    tools.press(make_event(Point(294, 294), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(600, 320), Qt::NoButton, Qt::LeftButton));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    REQUIRE(get_orientation(tools.get_cursor()) == 0);
    tools.move(make_event(Point(400, 500), Qt::NoButton, Qt::LeftButton));
    REQUIRE(get_orientation(tools.get_cursor()) == 2);
    tools.release(make_event(Point(400, 500), Qt::LeftButton, Qt::NoButton));
    editor.get_selection().clear();
    tools.set_tool("polygon");
    REQUIRE(hover(Point(600, 600)).m_kind == CursorKind::POLYGON);
    tools.set_tool("pen");
    REQUIRE(hover(Point(600, 600)).m_kind == CursorKind::PEN);
    tools.set_tool("artboard");
    REQUIRE(hover(Point(600, 600)).m_kind == CursorKind::ARTBOARD);
  }
}
