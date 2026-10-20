#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/PathCommands.hpp"
#include "Rellu/Geometry/Contour.hpp"
#include "Rellu/Tools/PenTool.hpp"
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
    tools.double_click(
      make_event(position, Qt::LeftButton, Qt::LeftButton));
    tools.release(make_event(position, Qt::LeftButton, Qt::NoButton));
  }

  struct Setup {
    Document m_document;
    NodeId m_artboard;
    Editor m_editor;
    ToolManager m_tools;

    Setup()
        : m_artboard(build_artboard(m_document)),
          m_editor(m_document),
          m_tools(m_editor) {
      m_tools.set_view(Matrix(), 1, Size(1000, 800));
      m_tools.set_tool("pen");
    }

    PenTool& get_pen() {
      return dynamic_cast<PenTool&>(m_tools.get_tool());
    }

    NodeId get_drawn() const {
      auto& nodes = m_editor.get_selection().get_nodes();
      REQUIRE(nodes.size() == 1);
      return nodes.front();
    }

    std::vector<Contour> get_contours(const NodeId& id) const {
      auto& geometry = m_document.get(id).get_shape()->m_geometry;
      return to_contours(*std::get<PathShape>(geometry).m_path);
    }
  };
}

TEST_SUITE("PenTool") {
  TEST_CASE("corner_points") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    auto& history = setup.m_editor.get_history();
    REQUIRE(tools.get_cursor().m_kind == CursorKind::CROSSHAIR);
    click(tools, Point(150, 150));
    auto id = setup.get_drawn();
    auto& node = setup.m_document.get(id);
    REQUIRE(node.m_parent == setup.m_artboard);
    REQUIRE(node.m_header.m_name == "Path 1");
    REQUIRE(node.m_header.m_transform == Matrix());
    REQUIRE(node.get_shape()->m_style == make_default_path_style());
    REQUIRE(setup.get_contours(id) ==
      std::vector{Contour(std::vector{make_anchor(Point(50, 50))}, false)});
    REQUIRE(setup.get_pen().get_path(tools.get_context()) == id);
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Draw Path");
    click(tools, Point(250, 150));
    click(tools, Point(250, 250));
    REQUIRE(history.get_undo_count() == 3);
    REQUIRE(history.get_undo_label() == "Add Anchor Point");
    auto path = Path();
    path.move_to(Point(50, 50));
    path.line_to(Point(150, 50));
    path.line_to(Point(150, 150));
    REQUIRE(*std::get<PathShape>(setup.m_document.get(id).get_shape()->
      m_geometry).m_path == path);
    tools.move(make_event(Point(300, 300), Qt::NoButton, Qt::NoButton));
    REQUIRE(!tools.draw().is_empty());
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Return, Qt::NoModifier)));
    REQUIRE(setup.get_pen().get_path(tools.get_context()).is_nil());
    REQUIRE(tools.get_tool_name() == "pen");
    click(tools, Point(600, 400));
    auto second = setup.get_drawn();
    REQUIRE(second != id);
    REQUIRE(setup.m_document.get(second).m_header.m_name == "Path 2");
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Escape, Qt::NoModifier)));
    REQUIRE(setup.get_pen().get_path(tools.get_context()).is_nil());
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Escape, Qt::NoModifier)));
    REQUIRE(tools.get_tool_name() == "select");
  }

  TEST_CASE("smooth_points") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    drag(tools, Point(150, 150), Point(150, 100));
    auto id = setup.get_drawn();
    drag(tools, Point(250, 150), Point(300, 150));
    drag(tools, Point(350, 250), Point(350, 300));
    REQUIRE(setup.m_editor.get_history().get_undo_count() == 3);
    auto contours = setup.get_contours(id);
    REQUIRE(contours.size() == 1);
    auto& anchors = contours.front().m_anchors;
    REQUIRE(anchors.size() == 3);
    REQUIRE(anchors[0].m_out == Point(50, 0));
    REQUIRE(anchors[1] == Anchor(Point(150, 50), Point(100, 50),
      Point(200, 50)));
    REQUIRE(is_smooth(anchors[1]));
    REQUIRE(anchors[2].m_in == Point(250, 100));
  }

  TEST_CASE("broken_handles") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    click(tools, Point(150, 150));
    auto id = setup.get_drawn();
    tools.press(make_event(Point(250, 150), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(300, 150), Qt::NoButton, Qt::LeftButton));
    tools.move(make_event(
      Point(300, 200), Qt::NoButton, Qt::LeftButton, Qt::AltModifier));
    tools.release(make_event(
      Point(300, 200), Qt::LeftButton, Qt::NoButton, Qt::AltModifier));
    click(tools, Point(350, 300));
    auto anchors = setup.get_contours(id).front().m_anchors;
    REQUIRE(anchors.size() == 3);
    REQUIRE(anchors[1] == Anchor(Point(150, 50), Point(100, 50),
      Point(200, 100)));
    REQUIRE(!is_smooth(anchors[1]));
  }

  TEST_CASE("constraint") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    click(tools, Point(150, 150));
    auto id = setup.get_drawn();
    click(tools, Point(250, 160), Qt::ShiftModifier);
    click(tools, Point(345, 250), Qt::ShiftModifier);
    drag(tools, Point(345, 350), Point(395, 360), Qt::ShiftModifier);
    auto anchors = setup.get_contours(id).front().m_anchors;
    REQUIRE(anchors.size() == 4);
    REQUIRE(anchors[1].m_point == Point(150, 50));
    REQUIRE(anchors[2].m_point.m_x == doctest::Approx(247.5));
    REQUIRE(anchors[2].m_point.m_y == doctest::Approx(147.5));
    REQUIRE(anchors[3].m_in.m_y == doctest::Approx(anchors[3].m_point.m_y));
  }

  TEST_CASE("close") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    click(tools, Point(150, 150));
    auto id = setup.get_drawn();
    click(tools, Point(250, 150));
    click(tools, Point(250, 250));
    click(tools, Point(153, 148));
    REQUIRE(setup.m_editor.get_history().get_undo_count() == 4);
    REQUIRE(setup.m_editor.get_history().get_undo_label() == "Close Path");
    REQUIRE(setup.get_pen().get_path(tools.get_context()).is_nil());
    auto contours = setup.get_contours(id);
    REQUIRE(contours.size() == 1);
    REQUIRE(contours.front().m_is_closed);
    REQUIRE(contours.front().m_anchors.size() == 3);
    click(tools, Point(500, 400));
    REQUIRE(setup.get_drawn() != id);
  }

  TEST_CASE("close_with_drag") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    click(tools, Point(150, 150));
    auto id = setup.get_drawn();
    click(tools, Point(250, 150));
    click(tools, Point(250, 250));
    drag(tools, Point(150, 150), Point(150, 200));
    auto contour = setup.get_contours(id).front();
    REQUIRE(contour.m_is_closed);
    REQUIRE(contour.m_anchors[0] == Anchor(Point(50, 50), Point(50, 0),
      Point(50, 100)));
    REQUIRE(setup.m_editor.get_history().get_undo_count() == 4);
  }

  TEST_CASE("toggle") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    click(tools, Point(150, 150));
    auto id = setup.get_drawn();
    click(tools, Point(250, 250));
    click(tools, Point(350, 150));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Return, Qt::NoModifier)));
    tools.double_click(make_event(Point(250, 250), Qt::LeftButton,
      Qt::LeftButton));
    tools.release(make_event(Point(250, 250), Qt::LeftButton, Qt::NoButton));
    auto anchors = setup.get_contours(id).front().m_anchors;
    REQUIRE(anchors.size() == 3);
    REQUIRE(is_smooth(anchors[1]));
    REQUIRE(anchors[1].m_in.m_y == doctest::Approx(150));
    REQUIRE(
      setup.m_editor.get_history().get_undo_label() == "Convert Point Type");
    tools.double_click(make_event(Point(250, 250), Qt::LeftButton,
      Qt::LeftButton));
    tools.release(make_event(Point(250, 250), Qt::LeftButton, Qt::NoButton));
    anchors = setup.get_contours(id).front().m_anchors;
    REQUIRE(anchors[1] == make_anchor(Point(150, 150)));
  }

  TEST_CASE("double_click") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    click(tools, Point(150, 150));
    auto id = setup.get_drawn();
    click(tools, Point(250, 250));
    double_click(tools, Point(350, 150));
    auto anchors = setup.get_contours(id).front().m_anchors;
    REQUIRE(anchors.size() == 3);
    REQUIRE(anchors[2].m_in.m_x == doctest::Approx(250 - 100.0 / 3));
    REQUIRE(anchors[2].m_in.m_y == doctest::Approx(50 + 100.0 / 3));
  }

  TEST_CASE("undo") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    auto& editor = setup.m_editor;
    click(tools, Point(150, 150));
    auto id = setup.get_drawn();
    click(tools, Point(250, 150));
    click(tools, Point(250, 250));
    REQUIRE(editor.undo());
    REQUIRE(setup.get_contours(id).front().m_anchors.size() == 2);
    REQUIRE(setup.get_pen().get_path(tools.get_context()) == id);
    click(tools, Point(150, 250));
    auto anchors = setup.get_contours(id).front().m_anchors;
    REQUIRE(anchors.size() == 3);
    REQUIRE(anchors[2].m_point == Point(50, 150));
    REQUIRE(editor.undo());
    REQUIRE(editor.undo());
    REQUIRE(editor.undo());
    REQUIRE(!setup.m_document.find(id));
    REQUIRE(setup.get_pen().get_path(tools.get_context()).is_nil());
    click(tools, Point(400, 400));
    REQUIRE(setup.get_drawn() != id);
    REQUIRE(setup.get_contours(setup.get_drawn()).front().m_anchors.size() ==
      1);
  }

  TEST_CASE("continuation") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    click(tools, Point(150, 150));
    auto id = setup.get_drawn();
    click(tools, Point(250, 150));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Escape, Qt::NoModifier)));
    auto& history = setup.m_editor.get_history();
    auto count = history.get_undo_count();
    click(tools, Point(250, 150));
    REQUIRE(setup.get_pen().get_path(tools.get_context()) == id);
    REQUIRE(history.get_undo_label() == "Extend Path");
    REQUIRE(history.get_undo_count() == count + 1);
    click(tools, Point(250, 250));
    REQUIRE(history.get_undo_label() == "Add Anchor Point");
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Return, Qt::NoModifier)));
    click(tools, Point(150, 150));
    REQUIRE(history.get_undo_label() == "Extend Path");
    click(tools, Point(150, 250));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Return, Qt::NoModifier)));
    auto contours = setup.get_contours(id);
    REQUIRE(contours.size() == 1);
    auto& anchors = contours.front().m_anchors;
    REQUIRE(anchors.size() == 4);
    REQUIRE(anchors[0].m_point == Point(150, 150));
    REQUIRE(anchors[1].m_point == Point(150, 50));
    REQUIRE(anchors[2].m_point == Point(50, 50));
    REQUIRE(anchors[3].m_point == Point(50, 150));
    REQUIRE(setup.m_document.get_children(setup.m_artboard).size() == 1);
  }

  TEST_CASE("added_point") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    click(tools, Point(150, 150));
    auto id = setup.get_drawn();
    click(tools, Point(350, 150));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Return, Qt::NoModifier)));
    click(tools, Point(250, 151));
    auto anchors = setup.get_contours(id).front().m_anchors;
    REQUIRE(anchors.size() == 3);
    REQUIRE(anchors[1].m_point == Point(150, 50));
    REQUIRE(
      setup.m_editor.get_history().get_undo_label() == "Add Anchor Point");
    REQUIRE(setup.get_pen().get_path(tools.get_context()).is_nil());
  }

  TEST_CASE("tool_change") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    click(tools, Point(150, 150));
    auto id = setup.get_drawn();
    click(tools, Point(250, 150));
    tools.set_tool("select");
    tools.set_tool("pen");
    REQUIRE(setup.get_pen().get_path(tools.get_context()).is_nil());
    REQUIRE(setup.get_contours(id).front().m_anchors.size() == 2);
  }

  TEST_CASE("snapping") {
    auto setup = Setup();
    click(setup.m_tools, Point(203, 302));
    click(setup.m_tools, Point(402, 397));
    auto id = setup.get_drawn();
    auto contours = setup.get_contours(id);
    auto transform = get_world_transform(setup.m_document, id);
    REQUIRE(transform.map(contours.front().m_anchors[0].m_point) ==
      Point(203, 300));
    REQUIRE(transform.map(contours.front().m_anchors[1].m_point) ==
      Point(400, 397));
    click(setup.m_tools, Point(599, 497), Qt::ControlModifier);
    contours = setup.get_contours(id);
    REQUIRE(transform.map(contours.front().m_anchors[2].m_point) ==
      Point(599, 497));
  }
}
