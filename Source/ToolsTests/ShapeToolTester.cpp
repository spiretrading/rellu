#include <numbers>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Localization/Localization.hpp"
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

  void hover(ToolManager& tools, Point position) {
    tools.move(make_event(position, Qt::NoButton, Qt::NoButton));
  }

  std::vector<Point> get_handle_centers(const CanvasOverlay& overlay) {
    auto centers = std::vector<Point>();
    for(auto& shape : overlay.m_shapes) {
      if(shape.m_radius > 0 && shape.m_fill == make_color(255, 255, 255)) {
        centers.push_back(shape.m_center);
      }
    }
    return centers;
  }
}

TEST_SUITE("ShapeTool") {
  TEST_CASE("localized_name") {
    set_language(Language::GERMAN);
    auto document = Document();
    build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("rectangle");
    drag(tools, Point(150, 150), Point(250, 200));
    auto name = get_drawn(editor).m_header.m_name;
    set_language(Language::ENGLISH);
    REQUIRE(name == "Rechteck 1");
  }

  TEST_CASE("rectangle") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("rectangle");
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RECTANGLE);
    drag(tools, Point(150.2, 149.8), Point(250, 200));
    auto& node = get_drawn(editor);
    REQUIRE(node.m_parent == artboard);
    REQUIRE(node.m_header.m_name == "Rectangle 1");
    REQUIRE(node.m_header.m_transform == make_translation(50, 50));
    REQUIRE(get_geometry(node) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 100, 50))));
    REQUIRE(node.get_shape()->m_style == make_default_style());
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.get_history().get_undo_label() == "Draw Rectangle");
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
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(50, 50));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 40, 40))));
    drag(tools, Point(500, 300), Point(460, 200), Qt::ShiftModifier);
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(360, 160));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 40, 40))));
    drag(tools, Point(300, 300), Point(340, 320), Qt::AltModifier);
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(160, 180));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 80, 40))));
    drag(tools, Point(300, 300), Point(260, 290),
      Qt::ShiftModifier | Qt::AltModifier);
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(190, 190));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 20, 20))));
    tools.press(make_event(Point(150, 150), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(250, 190), Qt::NoButton, Qt::LeftButton));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 100, 40))));
    tools.key_press(KeyEvent(Qt::Key_Shift, Qt::ShiftModifier));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 40, 40))));
    tools.key_release(KeyEvent(Qt::Key_Shift, Qt::NoModifier));
    tools.release(make_event(Point(250, 190), Qt::LeftButton, Qt::NoButton));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 100, 40))));
    REQUIRE(editor.get_history().get_undo_count() == 5);
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
    REQUIRE(editor.get_history().get_undo_label() == "Draw Ellipse");
    drag(tools, Point(300, 300), Point(340, 310), Qt::ShiftModifier);
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(EllipseShape(Point(5, 5), 5, 5)));
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
    REQUIRE(editor.get_history().get_undo_label() == "Draw Polygon");
    drag(tools, Point(300, 300), Point(400, 390), Qt::ShiftModifier);
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(200, 200));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(PolygonShape(3, Size(100, 87))));
    drag(tools, Point(600, 150), Point(550, 250), Qt::ShiftModifier);
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(450, 50));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(PolygonShape(3, Size(50, 43))));
    drag(tools, Point(500, 420), Point(550, 470),
      Qt::ShiftModifier | Qt::AltModifier);
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(350, 277));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(PolygonShape(3, Size(100, 86))));
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
    REQUIRE(node.m_header.m_transform == make_translation(50.5, 50.5));
    REQUIRE(get_geometry(node) ==
      ShapeGeometry(LineShape(Point(100, 0), Point(0, 50))));
    REQUIRE(node.get_shape()->m_style == make_default_line_style());
    REQUIRE(editor.get_history().get_undo_label() == "Draw Line");
    drag(tools, Point(150, 150), Point(240, 170), Qt::ShiftModifier);
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(LineShape(Point(0, 0), Point(90, 0))));
    drag(tools, Point(150, 250), Point(210, 300), Qt::ShiftModifier);
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(LineShape(Point(0, 0), Point(55, 55))));
    drag(tools, Point(200, 200), Point(220, 190), Qt::AltModifier);
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(78.5, 90.5));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(LineShape(Point(0, 21), Point(45, 0))));
  }

  TEST_CASE("line_snapping") {
    auto document = Document();
    auto artboard = build_artboard(document);
    {
      auto transaction = Transaction(document);
      transaction.append(artboard, make_header("Post",
        make_translation(200, 0)), make_rectangle_shape(10, 300));
      transaction.commit();
      document.clear_history();
    }
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("line");
    drag(tools, Point(150, 150), Point(248, 248), Qt::ShiftModifier);
    auto& node = get_drawn(editor);
    REQUIRE(node.m_header.m_transform == make_translation(50.5, 50.5));
    REQUIRE(get_geometry(node) ==
      ShapeGeometry(LineShape(Point(0, 0), Point(100, 100))));
    drag(tools, Point(150, 400), Point(297, 403));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(LineShape(Point(0, 0), Point(150, 0))));
    drag(tools, Point(150, 450), Point(297, 451), Qt::ControlModifier);
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(LineShape(Point(0, 0), Point(147, 1))));
  }

  TEST_CASE("line_to_stroked_edge") {
    auto document = Document();
    auto artboard = build_artboard(document);
    {
      auto transaction = Transaction(document);
      for(auto alignment : {StrokeAlignment::CENTER, StrokeAlignment::INSIDE,
          StrokeAlignment::OUTSIDE}) {
        auto shape = make_rectangle_shape(48, 50);
        shape.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2);
        shape.m_style.m_stroke->m_alignment = alignment;
        auto row = static_cast<double>(alignment) * 100;
        transaction.append(artboard, make_header("Stroked",
          make_translation(100, row)), shape);
      }
      transaction.commit();
      document.clear_history();
    }
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("line");
    for(auto row : {0.0, 100.0, 200.0}) {
      drag(tools, Point(150, row + 125), Point(246, row + 125));
      auto& node = get_drawn(editor);
      REQUIRE(node.m_header.m_transform ==
        make_translation(50.5, row + 25.5));
      REQUIRE(get_geometry(node) ==
        ShapeGeometry(LineShape(Point(0, 0), Point(98, 0))));
    }
  }

  TEST_CASE("line_on_grid") {
    auto document = Document();
    auto artboard = build_artboard(document);
    {
      auto transaction = Transaction(document);
      auto payload = *document.get(artboard).get_artboard();
      payload.m_layout_grid = LayoutGrid();
      payload.m_layout_grid->m_square_size = Size(8, 8);
      transaction.replace_payload(artboard, payload);
      transaction.commit();
      document.clear_history();
    }
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto settings = GridSettings();
    settings.m_is_square_visible = true;
    tools.get_context().set_grid_settings(settings);
    tools.set_tool("line");
    drag(tools, Point(153, 150), Point(230.3, 190.6));
    auto& node = get_drawn(editor);
    REQUIRE(node.m_header.m_transform == make_translation(56, 48));
    REQUIRE(get_geometry(node) ==
      ShapeGeometry(LineShape(Point(0, 0), Point(72, 40))));
    drag(tools, Point(303, 350), Point(380.3, 390.6), Qt::ControlModifier);
    REQUIRE(get_drawn(editor).m_header.m_transform ==
      make_translation(203.5, 250.5));
    REQUIRE(get_geometry(get_drawn(editor)) ==
      ShapeGeometry(LineShape(Point(0, 0), Point(77, 40))));
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

  TEST_CASE("selection_handles") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("rectangle");
    auto& history = editor.get_history();
    drag(tools, Point(200, 200), Point(300, 250));
    auto rectangle = get_drawn(editor).get_id();
    REQUIRE(get_handle_centers(tools.draw()).size() == 11);
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RESIZE);
    hover(tools, Point(250, 225));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RECTANGLE);
    auto centers = get_handle_centers(tools.draw());
    REQUIRE(centers.size() == 12);
    REQUIRE(centers[4] == Point(299.5, 249.5));
    REQUIRE(centers[8] == Point(213.5, 213.5));
    hover(tools, Point(250, 201));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RESIZE);
    hover(tools, Point(250, 191));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RECTANGLE);
    hover(tools, Point(305, 195));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    drag(tools, Point(300, 250), Point(340, 270));
    REQUIRE(tools.get_tool_name() == "rectangle");
    REQUIRE(editor.get_selection().get_nodes() == std::vector{rectangle});
    REQUIRE(*get_world_bounds(document, rectangle) == Rect(200, 200, 140, 70));
    REQUIRE(document.get(artboard).m_children.size() == 1);
    REQUIRE(history.get_undo_label() == "Resize");
    REQUIRE(history.get_undo_count() == 2);
    drag(tools, Point(214, 214), Point(224, 224));
    REQUIRE(std::get<RectangleShape>(get_geometry(get_drawn(editor))).m_radii ==
      CornerRadii(24, 24, 24, 24));
    REQUIRE(history.get_undo_label() == "Corner Radius");
    drag(tools, Point(345, 195), Point(310, 310));
    auto transform = get_world_transform(document, rectangle);
    REQUIRE(std::atan2(transform.m_b, transform.m_a) ==
      doctest::Approx(std::numbers::pi / 2));
    REQUIRE(history.get_undo_label() == "Rotate");
    REQUIRE(history.get_undo_count() == 4);
    tools.press(make_event(Point(500, 300), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(550, 350), Qt::NoButton, Qt::LeftButton));
    REQUIRE(tools.draw().m_shapes.empty());
    tools.release(make_event(Point(550, 350), Qt::LeftButton, Qt::NoButton));
    REQUIRE(document.get(artboard).m_children.size() == 2);
    REQUIRE(get_handle_centers(tools.draw()).size() == 11);
    REQUIRE(tools.get_tool_name() == "rectangle");
    tools.set_tool("ellipse");
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ELLIPSE);
    drag(tools, Point(550, 350), Point(560, 380));
    REQUIRE(document.get(artboard).m_children.size() == 2);
    REQUIRE(*get_world_bounds(document, get_drawn(editor).get_id()) ==
      Rect(500, 300, 60, 80));
  }

  TEST_CASE("line_handles") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("line");
    drag(tools, Point(250, 150), Point(150, 200));
    auto line = get_drawn(editor).get_id();
    REQUIRE(get_handle_centers(tools.draw()) ==
      std::vector{Point(250.5, 150.5)});
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    hover(tools, Point(200, 175));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::LINE);
    auto overlay = tools.draw();
    REQUIRE(overlay.m_shapes.size() == 2);
    REQUIRE(get_handle_centers(overlay) ==
      std::vector{Point(250.5, 150.5), Point(150.5, 200.5)});
    drag(tools, Point(150, 200), Point(170, 230));
    REQUIRE(document.get(artboard).m_children.size() == 1);
    REQUIRE(get_geometry(document.get(line)) ==
      ShapeGeometry(LineShape(Point(80, 0), Point(0, 80))));
    REQUIRE(document.get(line).m_header.m_transform ==
      make_translation(70.5, 50.5));
    REQUIRE(editor.get_history().get_undo_label() == "Drag Handle");
    REQUIRE(tools.get_tool_name() == "line");
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
  TEST_CASE("draw_onto_artboard") {
    auto document = Document();
    auto artboard = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("rectangle");
    drag(tools, Point(50, 50), Point(250, 200));
    auto& node = get_drawn(editor);
    REQUIRE(node.m_parent == artboard);
    REQUIRE(document.get(artboard).m_children.back() == node.get_id());
    REQUIRE(*get_world_bounds(document, node.get_id()) ==
      Rect(50, 50, 200, 150));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.get_history().get_undo_label() == "Draw Rectangle");
  }
}
