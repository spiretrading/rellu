#include <doctest/doctest.h>
#include "Rellu/Commands/ComponentCommands.hpp"
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

  TEST_CASE("entered_main_component") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto transaction = Transaction(document);
    auto group = transaction.append(
      artboard, make_header("Card", make_translation(50, 50)), Group());
    transaction.append(
      group, make_header("Face"), make_rectangle_shape(40, 40));
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto command = editor.execute(
      std::make_unique<MakeComponentCommand>(std::vector{group}));
    auto component =
      static_cast<const MakeComponentCommand&>(*command).get_main();
    REQUIRE(editor.get_context().enter(component));
    tools.set_tool("ellipse");
    drag(tools, Point(170, 170), Point(180, 180));
    auto& node = get_drawn(editor);
    REQUIRE(node.m_parent == component);
    REQUIRE(document.get(component).m_children.back() == node.get_id());
  }

  TEST_CASE("entered_repeat_grid") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto transaction = Transaction(document);
    auto role = Group();
    role.m_repeat_grid = RepeatGrid(Size(40, 40), 20, 10, 1, 3, Size(40, 140));
    auto grid = transaction.append(
      artboard, make_header("Grid", make_translation(300, 50)), role);
    auto cells = std::vector<NodeId>();
    for(auto i = 0; i != 3; ++i) {
      auto cell = transaction.append(
        grid, make_header("", make_translation(0, 50 * i)), Group());
      transaction.append(
        cell, make_header("Bar"), make_rectangle_shape(40, 20));
      cells.push_back(cell);
    }
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    REQUIRE(editor.get_context().enter(grid));
    REQUIRE(editor.get_context().enter(cells.front()));
    tools.set_tool("rectangle");
    drag(tools, Point(405, 175), Point(415, 185));
    REQUIRE(get_drawn(editor).m_parent == cells.front());
    for(auto& cell : cells) {
      auto& children = document.get(cell).m_children;
      REQUIRE(children.size() == 2);
      REQUIRE(*get_bounds(document, children.back(),
        get_transform(document, children.back())) == Rect(5, 25, 10, 10));
    }
    REQUIRE(editor.get_history().get_undo_count() == 1);
    editor.get_context().exit();
    drag(tools, Point(500, 400), Point(510, 410));
    REQUIRE(get_drawn(editor).m_parent == artboard);
  }

  TEST_CASE("entered_stack") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto transaction = Transaction(document);
    auto role = Group();
    role.m_stack = Stack(Orientation::VERTICAL);
    auto stack = transaction.append(
      artboard, make_header("Stack", make_translation(50, 250)), role);
    auto add = [&] (double y, double height) {
      return transaction.append(stack, make_header("Item",
        make_translation(0, y)), make_rectangle_shape(100, height));
    };
    auto last = add(90, 20);
    auto middle = add(50, 30);
    auto first = add(0, 40);
    auto payload = *document.get(stack).get_group();
    payload.m_stack->m_spacing.emplace(first, 10);
    payload.m_stack->m_spacing.emplace(middle, 10);
    transaction.replace_payload(stack, payload);
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    REQUIRE(editor.get_context().enter(stack));
    tools.set_tool("rectangle");
    drag(tools, Point(160, 410), Point(240, 420));
    auto drawn = get_drawn(editor).get_id();
    REQUIRE(document.get(stack).m_children ==
      std::vector{last, middle, drawn, first});
    REQUIRE(*get_world_bounds(document, drawn) == Rect(160, 400, 80, 10));
    REQUIRE(*get_world_bounds(document, middle) == Rect(150, 420, 100, 30));
    REQUIRE(*get_world_bounds(document, last) == Rect(150, 460, 100, 20));
    REQUIRE(document.get(stack).get_group()->m_stack->m_spacing.at(drawn) ==
      10);
    REQUIRE(editor.get_history().get_undo_count() == 1);
    editor.undo();
    REQUIRE(document.get(stack).m_children ==
      std::vector{last, middle, first});
    REQUIRE(*get_world_bounds(document, middle) == Rect(150, 400, 100, 30));
    tools.set_tool("pen");
    tools.press(make_event(Point(160, 455), Qt::LeftButton, Qt::LeftButton));
    tools.release(make_event(Point(160, 455), Qt::LeftButton, Qt::NoButton));
    auto& children = document.get(stack).m_children;
    REQUIRE(children.size() == 4);
    REQUIRE(children[0] == last);
    REQUIRE(children[2] == middle);
    REQUIRE(children[3] == first);
  }

  TEST_CASE("entered_instance") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto transaction = Transaction(document);
    auto group = transaction.append(
      artboard, make_header("Card", make_translation(50, 50)), Group());
    transaction.append(
      group, make_header("Face"), make_rectangle_shape(40, 40));
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto made = editor.execute(
      std::make_unique<MakeComponentCommand>(std::vector{group}));
    auto component =
      static_cast<const MakeComponentCommand&>(*made).get_component();
    auto inserted = editor.execute(std::make_unique<InsertInstanceCommand>(
      component, artboard, make_translation(300, 50)));
    auto instance =
      static_cast<const InsertInstanceCommand&>(*inserted).get_instance();
    REQUIRE(editor.get_context().enter(instance));
    tools.set_tool("rectangle");
    drag(tools, Point(410, 160), Point(420, 170));
    REQUIRE(get_drawn(editor).m_parent == instance);
  }
}
