#include <optional>
#include <doctest/doctest.h>
#include "Rellu/Tools/ToolManager.hpp"
#include "Rellu/Tools/ZoomTool.hpp"

using namespace Rellu;

namespace {
  PointerEvent make_event(Point position, Qt::MouseButton button,
      Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = {}) {
    return PointerEvent(position, button, buttons, modifiers);
  }

  void click(ToolManager& tools, Point position,
      Qt::KeyboardModifiers modifiers = {}) {
    tools.press(
      make_event(position, Qt::LeftButton, Qt::LeftButton, modifiers));
    tools.release(
      make_event(position, Qt::LeftButton, Qt::NoButton, modifiers));
  }

  void drag(ToolManager& tools, Point from, Point to) {
    tools.press(make_event(from, Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(to, Qt::NoButton, Qt::LeftButton));
    tools.release(make_event(to, Qt::LeftButton, Qt::NoButton));
  }

  void require_near(Point actual, Point expected) {
    REQUIRE(actual.m_x == doctest::Approx(expected.m_x));
    REQUIRE(actual.m_y == doctest::Approx(expected.m_y));
  }
}

TEST_SUITE("ZoomTool") {
  TEST_CASE("click") {
    auto document = Document();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("zoom");
    auto& context = tools.get_context();
    click(tools, Point(100, 50));
    REQUIRE(context.get_zoom() == doctest::Approx(ZoomTool::ZOOM_STEP));
    require_near(context.to_view(Point(100, 50)), Point(100, 50));
    click(tools, Point(300, 200));
    REQUIRE(context.get_zoom() == doctest::Approx(4));
    click(tools, Point(300, 200), Qt::AltModifier);
    REQUIRE(context.get_zoom() == doctest::Approx(2));
    require_near(context.to_view(Point(100, 50)), Point(100, 50));
    REQUIRE(tools.get_tool_name() == "zoom");
  }

  TEST_CASE("drag") {
    auto document = Document();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("zoom");
    auto& context = tools.get_context();
    tools.press(make_event(Point(300, 200), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(100, 100), Qt::NoButton, Qt::LeftButton));
    REQUIRE(tools.draw().m_shapes.size() == 1);
    tools.release(make_event(Point(100, 100), Qt::LeftButton, Qt::NoButton));
    REQUIRE(context.get_zoom() == doctest::Approx(5));
    require_near(context.to_view(Point(200, 150)), Point(500, 400));
    REQUIRE(tools.draw().m_shapes.empty());
  }

  TEST_CASE("navigator") {
    auto document = Document();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("zoom");
    auto factor = 0.0;
    auto center = Point(0, 0);
    auto region = std::optional<Rect>();
    tools.get_context().set_navigator(ViewNavigator(
      [&] (const auto& zoom, const auto& point) {
        factor = zoom;
        center = point;
      }, [&] (const auto& rect) {
        region = rect;
      }));
    click(tools, Point(40, 30), Qt::AltModifier);
    REQUIRE(factor == 1 / ZoomTool::ZOOM_STEP);
    REQUIRE(center == Point(40, 30));
    drag(tools, Point(10, 20), Point(110, 70));
    REQUIRE(region == Rect(10, 20, 100, 50));
    REQUIRE(tools.get_context().get_zoom() == 1);
  }

  TEST_CASE("escape") {
    auto document = Document();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("zoom");
    tools.press(make_event(Point(300, 200), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(100, 100), Qt::NoButton, Qt::LeftButton));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Escape, Qt::NoModifier)));
    tools.release(make_event(Point(100, 100), Qt::LeftButton, Qt::NoButton));
    REQUIRE(tools.get_context().get_zoom() == 1);
    REQUIRE(tools.get_tool_name() == "zoom");
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Escape, Qt::NoModifier)));
    REQUIRE(tools.get_tool_name() == "select");
  }

  TEST_CASE("cursor") {
    auto document = Document();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("zoom");
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ZOOM_IN);
    tools.move(make_event(Point(10, 10), Qt::NoButton, Qt::NoButton,
      Qt::AltModifier));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ZOOM_OUT);
    tools.key_release(KeyEvent(Qt::Key_Alt, Qt::NoModifier));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ZOOM_IN);
  }
}
