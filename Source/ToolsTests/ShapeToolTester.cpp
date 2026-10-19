#include <doctest/doctest.h>
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Tools/ShapeTool.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId build_artboard(Document& document) {
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 100, 600, 400);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    auto id = transaction.append(NodeId(), make_header("Board"), artboard);
    transaction.commit();
    document.clear_history();
    return id;
  }

  PointerEvent make_event(Point position, Qt::MouseButton button,
      Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = {}) {
    return PointerEvent(position, button, buttons, modifiers);
  }

  void drag(ToolManager& tools, Point from, Point to,
      Qt::KeyboardModifiers modifiers = {}) {
    tools.press(make_event(from, Qt::LeftButton, Qt::LeftButton, modifiers));
    for(auto i = 1; i <= 4; ++i) {
      tools.move(make_event(interpolate(from, to, i / 4.0), Qt::NoButton,
        Qt::LeftButton, modifiers));
    }
    tools.release(make_event(to, Qt::LeftButton, Qt::NoButton, modifiers));
  }

  const Node& get_drawn(const Editor& editor) {
    auto& nodes = editor.get_selection().get_nodes();
    REQUIRE(nodes.size() == 1);
    return editor.get_document().get(nodes.front());
  }

  const ShapeGeometry& get_geometry(const Node& node) {
    return node.get_shape()->m_geometry;
  }
}

TEST_SUITE("ShapeTool") {
  TEST_CASE("rectangle") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("rectangle");
    REQUIRE(tools.get_cursor().m_kind == CursorKind::CROSSHAIR);
    drag(tools, Point(150.2, 149.8), Point(250, 200));
    auto& node = get_drawn(editor);
    REQUIRE(node.m_parent == artboard);
    REQUIRE(node.m_header.m_name == "Rectangle 1");
    REQUIRE(node.m_header.m_transform == make_translation(50, 50));
    REQUIRE(get_geometry(node) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 100, 50))));
    REQUIRE(node.get_shape()->m_style == make_default_style());
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.get_history().get_undo_label() == "Rectangle");
    REQUIRE(tools.get_tool_name() == "rectangle");
    drag(tools, Point(800, 600), Point(820, 640));
    auto& loose = get_drawn(editor);
    REQUIRE(loose.m_parent.is_nil());
    REQUIRE(loose.m_header.m_name == "Rectangle 2");
    REQUIRE(loose.m_header.m_transform == make_translation(800, 600));
    REQUIRE(get_geometry(loose) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 20, 40))));
    REQUIRE(editor.get_history().get_undo_count() == 2);
    REQUIRE(editor.undo());
    REQUIRE(document.get_canvas() == std::vector{artboard});
  }

  TEST_CASE("constraints") {
    auto document = Document();
    build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("rectangle");
    drag(tools, Point(150, 150), Point(250, 190), Qt::ShiftModifier);
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 100, 100))));
    drag(tools, Point(300, 300), Point(340, 320), Qt::AltModifier);
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(160, 180));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 80, 40))));
    drag(tools, Point(300, 300), Point(260, 290),
      Qt::ShiftModifier | Qt::AltModifier);
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(160, 160));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 80, 80))));
    tools.press(make_event(Point(150, 150), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(250, 190), Qt::NoButton, Qt::LeftButton));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 100, 40))));
    tools.key_press(KeyEvent(Qt::Key_Shift, Qt::ShiftModifier));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 100, 100))));
    tools.key_release(KeyEvent(Qt::Key_Shift, Qt::NoModifier));
    tools.release(make_event(Point(250, 190), Qt::LeftButton, Qt::NoButton));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 100, 40))));
    REQUIRE(editor.get_history().get_undo_count() == 4);
  }

  TEST_CASE("ellipse") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("ellipse");
    drag(tools, Point(200, 200), Point(140, 170));
    auto& node = get_drawn(editor);
    REQUIRE(node.m_parent == artboard);
    REQUIRE(node.m_header.m_name == "Ellipse 1");
    REQUIRE(node.m_header.m_transform == make_translation(40, 70));
    REQUIRE(get_geometry(node) ==
      ShapeGeometry(EllipseShape(Point(30, 15), 30, 15)));
    REQUIRE(node.get_shape()->m_style == make_default_style());
    REQUIRE(editor.get_history().get_undo_label() == "Ellipse");
    drag(tools, Point(200, 200), Point(240, 210), Qt::ShiftModifier);
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(EllipseShape(Point(20, 20), 20, 20)));
    REQUIRE(get_drawn(editor).m_header.m_name == "Ellipse 2");
  }

  TEST_CASE("polygon") {
    auto document = Document();
    build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("polygon");
    drag(tools, Point(200, 200), Point(260, 250));
    auto& node = get_drawn(editor);
    REQUIRE(node.m_header.m_name == "Polygon 1");
    REQUIRE(get_geometry(node) ==
      ShapeGeometry(PolygonShape(3, Size(60, 50))));
    REQUIRE(node.get_shape()->m_style == make_default_style());
    REQUIRE(editor.get_history().get_undo_label() == "Polygon");
  }

  TEST_CASE("line") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("line");
    drag(tools, Point(250, 150), Point(150, 200));
    auto& node = get_drawn(editor);
    REQUIRE(node.m_parent == artboard);
    REQUIRE(node.m_header.m_name == "Line 1");
    REQUIRE(node.m_header.m_transform == make_translation(50, 50));
    REQUIRE(get_geometry(node) ==
      ShapeGeometry(LineShape(Point(100, 0), Point(0, 50))));
    REQUIRE(node.get_shape()->m_style == make_default_line_style());
    REQUIRE(editor.get_history().get_undo_label() == "Line");
    drag(tools, Point(150, 150), Point(250, 170), Qt::ShiftModifier);
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(LineShape(Point(0, 0), Point(100, 0))));
    drag(tools, Point(150, 150), Point(210, 200), Qt::ShiftModifier);
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(LineShape(Point(0, 0), Point(55, 55))));
    drag(tools, Point(200, 200), Point(220, 190), Qt::AltModifier);
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(80, 90));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(LineShape(Point(0, 20), Point(40, 0))));
  }

  TEST_CASE("preview") {
    auto document = Document();
    build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("rectangle");
    tools.press(make_event(Point(200, 200), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(201, 201), Qt::NoButton, Qt::LeftButton));
    REQUIRE(editor.get_selection().is_empty());
    tools.move(make_event(Point(220, 230), Qt::NoButton, Qt::LeftButton));
    REQUIRE(tools.is_busy());
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 20, 30))));
    tools.move(make_event(Point(260, 240), Qt::NoButton, Qt::LeftButton));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 60, 40))));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    tools.release(make_event(Point(260, 240), Qt::LeftButton, Qt::NoButton));
    REQUIRE(!tools.is_busy());
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(!tools.draw().m_shapes.empty());
  }

  TEST_CASE("escape") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("ellipse");
    tools.press(make_event(Point(200, 200), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(260, 240), Qt::NoButton, Qt::LeftButton));
    REQUIRE(document.get(artboard).m_children.size() == 1);
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Escape, Qt::NoModifier)));
    REQUIRE(document.get(artboard).m_children.empty());
    REQUIRE(editor.get_selection().is_empty());
    REQUIRE(!tools.is_busy());
    REQUIRE(tools.get_tool_name() == "ellipse");
    tools.release(make_event(Point(260, 240), Qt::LeftButton, Qt::NoButton));
    REQUIRE(document.get(artboard).m_children.empty());
    REQUIRE(!editor.get_history().can_undo());
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Escape, Qt::NoModifier)));
    REQUIRE(tools.get_tool_name() == "select");
  }

  TEST_CASE("click") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("rectangle");
    drag(tools, Point(200, 200), Point(201, 202));
    REQUIRE(document.get(artboard).m_children.empty());
    REQUIRE(!editor.get_history().can_undo());
  }

  TEST_CASE("zoomed_view") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(make_translation(-100, -100) * make_scale(2, 2), 2,
      Size(1000, 800));
    tools.set_tool("rectangle");
    drag(tools, Point(200, 200), Point(300, 260));
    auto& node = get_drawn(editor);
    REQUIRE(node.m_parent == artboard);
    REQUIRE(node.m_header.m_transform == make_translation(50, 50));
    REQUIRE(get_geometry(node) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 50, 30))));
  }

  TEST_CASE("entered_group") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto transaction = Transaction(document);
    auto group = transaction.append(
      artboard, make_header("Group", make_translation(50, 50)), Group());
    auto inner = transaction.append(
      group, make_header("Rectangle"), make_rectangle_shape(40, 40));
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    REQUIRE(editor.get_context().enter(group));
    tools.set_tool("ellipse");
    drag(tools, Point(300, 300), Point(340, 320));
    auto& node = get_drawn(editor);
    REQUIRE(node.m_parent == group);
    REQUIRE(document.get(group).m_children ==
      std::vector{inner, node.get_id()});
    REQUIRE(*get_world_bounds(document, node.get_id()) ==
      Rect(300, 300, 40, 20));
    REQUIRE(editor.get_context().get_container() == group);
    editor.get_context().exit();
    drag(tools, Point(400, 300), Point(440, 320));
    REQUIRE(get_drawn(editor).m_parent == artboard);
  }
}
