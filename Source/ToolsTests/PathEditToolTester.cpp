#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Contour.hpp"
#include "Rellu/Tools/PathEditTool.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

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

  void drag(ToolManager& tools, Point from, Point to,
      Qt::KeyboardModifiers modifiers = {}) {
    tools.press(make_event(from, Qt::LeftButton, Qt::LeftButton, modifiers));
    for(auto i = 1; i <= 4; ++i) {
      tools.move(make_event(interpolate(from, to, i / 4.0), Qt::NoButton,
        Qt::LeftButton, modifiers));
    }
    tools.release(make_event(to, Qt::LeftButton, Qt::NoButton, modifiers));
  }

  void double_click(ToolManager& tools, Point position) {
    click(tools, position);
    tools.double_click(make_event(position, Qt::LeftButton, Qt::LeftButton));
    tools.release(make_event(position, Qt::LeftButton, Qt::NoButton));
  }

  struct Shapes {
    NodeId m_artboard;
    NodeId m_rectangle;
    NodeId m_ellipse;
  };

  Shapes build_shapes(Document& document) {
    auto shapes = Shapes();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 100, 600, 400);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    shapes.m_artboard =
      transaction.append(NodeId(), make_header("Board"), artboard);
    shapes.m_rectangle = transaction.append(shapes.m_artboard,
      make_header("Rectangle", make_translation(50, 50)),
      make_rectangle_shape(100, 50));
    auto ellipse = Shape();
    ellipse.m_geometry = EllipseShape(Point(50, 50), 50, 50);
    ellipse.m_style.m_fill = SolidFill(make_color(0, 0, 255));
    shapes.m_ellipse = transaction.append(shapes.m_artboard,
      make_header("Ellipse", make_translation(300, 100)), ellipse);
    transaction.commit();
    document.clear_history();
    return shapes;
  }

  struct Setup {
    Document m_document;
    Shapes m_shapes;
    NodeId m_artboard;
    NodeId m_rectangle;
    NodeId m_ellipse;
    Editor m_editor;
    ToolManager m_tools;

    Setup()
        : m_shapes(build_shapes(m_document)),
          m_artboard(m_shapes.m_artboard),
          m_rectangle(m_shapes.m_rectangle),
          m_ellipse(m_shapes.m_ellipse),
          m_editor(m_document),
          m_tools(m_editor) {
      m_tools.set_view(Matrix(), 1, Size(1000, 800));
    }

    PathEditTool& get_tool() {
      return dynamic_cast<PathEditTool&>(m_tools.get_tool());
    }

    void edit(const NodeId& id) {
      m_editor.get_selection().select(id);
      m_tools.set_tool("path_edit");
    }

    std::vector<Contour> get_contours(const NodeId& id) const {
      return to_contours(to_path(m_document.get(id).get_shape()->m_geometry));
    }

    bool is_path(const NodeId& id) const {
      return std::holds_alternative<PathShape>(
        m_document.get(id).get_shape()->m_geometry);
    }
  };
}

TEST_SUITE("PathEditTool") {
  TEST_CASE("entering") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    double_click(tools, Point(200, 175));
    REQUIRE(tools.get_tool_name() == "path_edit");
    REQUIRE(setup.m_editor.get_selection().get_nodes() ==
      std::vector{setup.m_rectangle});
    REQUIRE(setup.get_tool().get_node(tools.get_context()) ==
      setup.m_rectangle);
    REQUIRE(!tools.draw().is_empty());
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Escape, Qt::NoModifier)));
    REQUIRE(tools.get_tool_name() == "select");
    REQUIRE(setup.m_editor.get_selection().get_nodes() ==
      std::vector{setup.m_rectangle});
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Return, Qt::NoModifier)));
    REQUIRE(tools.get_tool_name() == "path_edit");
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Enter, Qt::NoModifier)));
    REQUIRE(tools.get_tool_name() == "select");
    setup.m_editor.get_selection().select(setup.m_artboard);
    REQUIRE(!tools.key_press(KeyEvent(Qt::Key_Return, Qt::NoModifier)));
    REQUIRE(tools.get_tool_name() == "select");
  }

  TEST_CASE("move_point") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    auto& history = setup.m_editor.get_history();
    setup.edit(setup.m_rectangle);
    drag(tools, Point(150, 150), Point(140, 130));
    REQUIRE(setup.is_path(setup.m_rectangle));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Move Anchor Point");
    auto contours = setup.get_contours(setup.m_rectangle);
    REQUIRE(contours.size() == 1);
    REQUIRE(contours.front().m_is_closed);
    REQUIRE(contours.front().m_anchors.size() == 4);
    REQUIRE(contours.front().m_anchors[0] == make_anchor(Point(-10, -20)));
    REQUIRE(contours.front().m_anchors[1] == make_anchor(Point(100, 0)));
    REQUIRE(setup.get_tool().get_selection() ==
      std::vector{AnchorIndex(0, 0)});
    REQUIRE(tools.get_tool_name() == "path_edit");
    REQUIRE(setup.m_editor.undo());
    REQUIRE(setup.m_document.get(setup.m_rectangle).get_shape()->
      m_geometry == ShapeGeometry(RectangleShape(Rect(0, 0, 100, 50))));
  }

  TEST_CASE("selection") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    setup.edit(setup.m_rectangle);
    click(tools, Point(250, 150));
    REQUIRE(setup.get_tool().get_selection() ==
      std::vector{AnchorIndex(0, 1)});
    click(tools, Point(250, 200), Qt::ShiftModifier);
    REQUIRE(setup.get_tool().get_selection() ==
      std::vector{AnchorIndex(0, 1), AnchorIndex(0, 2)});
    click(tools, Point(250, 150), Qt::ShiftModifier);
    REQUIRE(setup.get_tool().get_selection() ==
      std::vector{AnchorIndex(0, 2)});
    drag(tools, Point(130, 130), Point(260, 170));
    REQUIRE(setup.get_tool().get_selection() ==
      std::vector{AnchorIndex(0, 0), AnchorIndex(0, 1)});
    REQUIRE(tools.get_tool_name() == "path_edit");
    REQUIRE(setup.m_editor.get_history().get_undo_count() == 0);
    click(tools, Point(200, 175));
    REQUIRE(setup.get_tool().get_selection().empty());
    REQUIRE(tools.get_tool_name() == "path_edit");
  }

  TEST_CASE("nudge") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    setup.edit(setup.m_rectangle);
    drag(tools, Point(130, 130), Point(260, 170));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Right, Qt::NoModifier)));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Right, Qt::NoModifier, true)));
    REQUIRE(tools.key_release(KeyEvent(Qt::Key_Right, Qt::NoModifier)));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Down, Qt::ShiftModifier)));
    REQUIRE(tools.key_release(KeyEvent(Qt::Key_Down, Qt::ShiftModifier)));
    REQUIRE(setup.m_editor.get_history().get_undo_count() == 2);
    auto anchors = setup.get_contours(setup.m_rectangle).front().m_anchors;
    REQUIRE(anchors[0].m_point == Point(2, 10));
    REQUIRE(anchors[1].m_point == Point(102, 10));
    REQUIRE(anchors[2].m_point == Point(100, 50));
  }

  TEST_CASE("added_point") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    setup.edit(setup.m_rectangle);
    click(tools, Point(200, 151));
    auto anchors = setup.get_contours(setup.m_rectangle).front().m_anchors;
    REQUIRE(anchors.size() == 5);
    REQUIRE(anchors[1] == make_anchor(Point(50, 0)));
    REQUIRE(setup.get_tool().get_selection() ==
      std::vector{AnchorIndex(0, 1)});
    REQUIRE(
      setup.m_editor.get_history().get_undo_label() == "Add Anchor Point");
    drag(tools, Point(200, 150), Point(200, 140));
    anchors = setup.get_contours(setup.m_rectangle).front().m_anchors;
    REQUIRE(anchors[1] == make_anchor(Point(50, -10)));
    REQUIRE(setup.m_editor.get_history().get_undo_count() == 2);
  }

  TEST_CASE("deletion") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    setup.edit(setup.m_rectangle);
    auto& tool = setup.get_tool();
    REQUIRE(!tool.is_edit_key(tools.get_context(),
      KeyEvent(Qt::Key_Delete, Qt::NoModifier)));
    click(tools, Point(250, 200));
    REQUIRE(tool.is_edit_key(tools.get_context(),
      KeyEvent(Qt::Key_Delete, Qt::NoModifier)));
    REQUIRE(!tool.is_edit_key(tools.get_context(),
      KeyEvent(Qt::Key_A, Qt::NoModifier)));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Delete, Qt::NoModifier)));
    auto contours = setup.get_contours(setup.m_rectangle);
    REQUIRE(contours.front().m_is_closed);
    REQUIRE(contours.front().m_anchors.size() == 3);
    REQUIRE(contours.front().m_anchors[2] == make_anchor(Point(0, 50)));
    REQUIRE(tool.get_selection().empty());
    drag(tools, Point(130, 130), Point(270, 220));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Backspace, Qt::NoModifier)));
    REQUIRE(!setup.m_document.find(setup.m_rectangle));
    REQUIRE(tools.get_tool_name() == "select");
    REQUIRE(setup.m_editor.undo());
    REQUIRE(setup.m_document.find(setup.m_rectangle));
  }

  TEST_CASE("handles") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    setup.edit(setup.m_ellipse);
    click(tools, Point(500, 250));
    auto anchors = setup.get_contours(setup.m_ellipse).front().m_anchors;
    REQUIRE(anchors[0].m_point == Point(100, 50));
    REQUIRE(is_smooth(anchors[0]));
    auto handle = anchors[0].m_out + Point(400, 200);
    auto opposite = distance(anchors[0].m_in, anchors[0].m_point);
    drag(tools, handle, Point(540, 250));
    anchors = setup.get_contours(setup.m_ellipse).front().m_anchors;
    REQUIRE(anchors[0].m_out == Point(140, 50));
    REQUIRE(is_smooth(anchors[0]));
    REQUIRE(anchors[0].m_in.m_x == doctest::Approx(100 - opposite));
    REQUIRE(anchors[0].m_in.m_y == doctest::Approx(50));
    drag(tools, Point(540, 250), Point(540, 200), Qt::AltModifier);
    anchors = setup.get_contours(setup.m_ellipse).front().m_anchors;
    REQUIRE(anchors[0].m_out == Point(140, 0));
    REQUIRE(anchors[0].m_in.m_x == doctest::Approx(100 - opposite));
    REQUIRE(anchors[0].m_in.m_y == doctest::Approx(50));
    REQUIRE(!is_smooth(anchors[0]));
    REQUIRE(setup.m_editor.get_history().get_undo_count() == 2);
  }

  TEST_CASE("toggle") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    setup.edit(setup.m_rectangle);
    double_click(tools, Point(250, 150));
    auto anchors = setup.get_contours(setup.m_rectangle).front().m_anchors;
    REQUIRE(is_smooth(anchors[1]));
    REQUIRE(setup.m_editor.get_history().get_undo_count() == 1);
    double_click(tools, Point(250, 150));
    anchors = setup.get_contours(setup.m_rectangle).front().m_anchors;
    REQUIRE(anchors[1] == make_anchor(Point(100, 0)));
    REQUIRE(setup.m_editor.get_history().get_undo_count() == 2);
  }

  TEST_CASE("click_away") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    setup.edit(setup.m_rectangle);
    click(tools, Point(450, 250));
    REQUIRE(tools.get_tool_name() == "select");
    REQUIRE(setup.m_editor.get_selection().get_nodes() ==
      std::vector{setup.m_ellipse});
    setup.edit(setup.m_rectangle);
    click(tools, Point(900, 700));
    REQUIRE(tools.get_tool_name() == "select");
    REQUIRE(setup.m_editor.get_selection().is_empty());
    setup.edit(setup.m_rectangle);
    setup.m_editor.get_selection().select(
      std::vector{setup.m_rectangle, setup.m_ellipse});
    click(tools, Point(150, 150));
    REQUIRE(tools.get_tool_name() == "select");
  }

  TEST_CASE("snapping") {
    auto setup = Setup();
    setup.edit(setup.m_rectangle);
    drag(setup.m_tools, Point(150, 150), Point(198, 247));
    auto contours = setup.get_contours(setup.m_rectangle);
    auto transform = get_world_transform(setup.m_document, setup.m_rectangle);
    REQUIRE(transform.map(contours.front().m_anchors[0].m_point) ==
      Point(198, 250));
  }
}
