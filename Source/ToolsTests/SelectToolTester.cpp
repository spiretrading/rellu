#include <cmath>
#include <numbers>
#include <doctest/doctest.h>
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Tools/SelectTool.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Board {
    NodeId m_artboard;
    NodeId m_group;
    NodeId m_first;
    NodeId m_second;
    NodeId m_rectangle;
    NodeId m_locked;
  };

  NodeId add_rectangle(Transaction& transaction, const NodeId& parent,
      const Rect& rect) {
    return transaction.append(parent, make_header("Rectangle",
      make_translation(rect.m_x, rect.m_y)),
      make_rectangle_shape(rect.m_width, rect.m_height));
  }

  Board build_board(Document& document) {
    auto board = Board();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 100, 600, 400);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    board.m_artboard =
      transaction.append(NodeId(), make_header("Board"), artboard);
    board.m_group = transaction.append(board.m_artboard,
      make_header("Group", make_translation(50, 50)), Group());
    board.m_first =
      add_rectangle(transaction, board.m_group, Rect(0, 0, 40, 40));
    board.m_second =
      add_rectangle(transaction, board.m_group, Rect(60, 0, 40, 40));
    board.m_rectangle =
      add_rectangle(transaction, board.m_artboard, Rect(300, 50, 100, 50));
    board.m_locked =
      add_rectangle(transaction, board.m_artboard, Rect(300, 200, 50, 50));
    auto header = document.get(board.m_locked).m_header;
    header.m_is_locked = true;
    transaction.replace_header(header);
    transaction.commit();
    document.clear_history();
    return board;
  }

  PointerEvent make_event(Point position, Qt::MouseButton button,
      Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
    return PointerEvent(position, button, buttons, modifiers);
  }

  void click(ToolManager& tools, Point position,
      Qt::KeyboardModifiers modifiers = {}) {
    tools.press(
      make_event(position, Qt::LeftButton, Qt::LeftButton, modifiers));
    tools.release(
      make_event(position, Qt::LeftButton, Qt::NoButton, modifiers));
  }

  void hover(ToolManager& tools, Point position,
      Qt::KeyboardModifiers modifiers = {}) {
    tools.move(make_event(position, Qt::NoButton, Qt::NoButton, modifiers));
  }

  void drag_to(ToolManager& tools, Point from, Point to,
      Qt::KeyboardModifiers modifiers = {}) {
    for(auto i = 1; i <= 4; ++i) {
      tools.move(make_event(interpolate(from, to, i / 4.0), Qt::NoButton,
        Qt::LeftButton, modifiers));
    }
  }

  void drag(ToolManager& tools, Point from, Point to,
      Qt::KeyboardModifiers modifiers = {}) {
    tools.press(make_event(from, Qt::LeftButton, Qt::LeftButton, modifiers));
    drag_to(tools, from, to, modifiers);
    tools.release(make_event(to, Qt::LeftButton, Qt::NoButton, modifiers));
  }

  void type(ToolManager& tools, int key, Qt::KeyboardModifiers modifiers = {},
      bool is_repeat = false) {
    tools.key_press(KeyEvent(key, modifiers, is_repeat));
    tools.key_release(KeyEvent(key, modifiers, is_repeat));
  }

  Rect get_bounds(const Document& document, const NodeId& id) {
    return *get_world_bounds(document, id);
  }

  void require_near(Point actual, Point expected) {
    REQUIRE(actual.m_x == doctest::Approx(expected.m_x));
    REQUIRE(actual.m_y == doctest::Approx(expected.m_y));
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

  void require_sizes(const CanvasOverlay& overlay) {
    for(auto& shape : overlay.m_shapes) {
      if(shape.m_radius > 0) {
        REQUIRE(shape.m_size ==
          Size(2 * shape.m_radius, 2 * shape.m_radius));
      } else {
        REQUIRE(shape.m_size.m_height == 1);
      }
    }
  }
}

TEST_SUITE("SelectTool") {
  TEST_CASE("click") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& selection = editor.get_selection();
    click(tools, Point(160, 160));
    REQUIRE(selection.get_nodes() == std::vector{board.m_group});
    click(tools, Point(420, 170));
    REQUIRE(selection.get_nodes() == std::vector{board.m_rectangle});
    click(tools, Point(300, 400));
    REQUIRE(selection.is_empty());
    REQUIRE(editor.get_context().get_artboard() == board.m_artboard);
    click(tools, Point(420, 170));
    click(tools, Point(50, 50));
    REQUIRE(selection.is_empty());
    click(tools, Point(420, 320));
    REQUIRE(selection.get_nodes() == std::vector{board.m_locked});
  }

  TEST_CASE("double_click") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& selection = editor.get_selection();
    auto& context = editor.get_context();
    click(tools, Point(160, 160));
    tools.double_click(make_event(Point(160, 160), Qt::LeftButton,
      Qt::LeftButton, Qt::NoModifier));
    tools.release(make_event(
      Point(160, 160), Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    REQUIRE(context.get_container() == board.m_group);
    REQUIRE(selection.get_nodes() == std::vector{board.m_first});
    click(tools, Point(220, 160));
    REQUIRE(selection.get_nodes() == std::vector{board.m_second});
    REQUIRE(context.get_container() == board.m_group);
    type(tools, Qt::Key_Escape);
    REQUIRE(context.get_container().is_nil());
    REQUIRE(selection.get_nodes() == std::vector{board.m_group});
    type(tools, Qt::Key_Escape);
    REQUIRE(selection.is_empty());
    tools.double_click(make_event(Point(160, 160), Qt::LeftButton,
      Qt::LeftButton, Qt::NoModifier));
    click(tools, Point(420, 170));
    REQUIRE(context.get_container().is_nil());
    REQUIRE(selection.get_nodes() == std::vector{board.m_rectangle});
  }

  TEST_CASE("instance_text") {
    auto document = Document();
    auto board = build_board(document);
    auto instance = NodeId();
    {
      auto transaction = Transaction(document);
      auto master = transaction.insert_root(make_header("Label"), Group());
      transaction.append(master, make_header("Text"), make_text(u"Label"));
      auto component = Component();
      component.m_name = "Label";
      component.m_master = master;
      auto id = transaction.add_component(component);
      instance = insert_instance(document, transaction, id, board.m_artboard,
        document.get_children(board.m_artboard).size(),
        make_translation(50, 200));
      transaction.commit();
      document.clear_history();
    }
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto reference = document.get(instance).m_children.front();
    for(auto i = 0; i != 2; ++i) {
      tools.double_click(make_event(Point(170, 315), Qt::LeftButton,
        Qt::LeftButton, Qt::NoModifier));
      tools.release(make_event(
        Point(170, 315), Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    }
    REQUIRE(editor.get_context().get_container() == instance);
    REQUIRE(document.get(reference).get_text());
    REQUIRE(tools.get_tool_name() == "text");
    REQUIRE(editor.get_history().get_undo_label() == "Edit Text");
  }

  TEST_CASE("deep_select") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    click(tools, Point(220, 160), Qt::ControlModifier);
    REQUIRE(editor.get_selection().get_nodes() == std::vector{board.m_second});
    REQUIRE(editor.get_context().get_container() == board.m_group);
    click(tools, Point(420, 170), Qt::ControlModifier);
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector{board.m_rectangle});
    REQUIRE(editor.get_context().get_container().is_nil());
  }

  TEST_CASE("toggle") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& selection = editor.get_selection();
    auto both = std::vector{board.m_group, board.m_rectangle};
    click(tools, Point(160, 160));
    click(tools, Point(420, 170), Qt::ShiftModifier);
    REQUIRE(selection.get_nodes() == both);
    click(tools, Point(160, 160));
    REQUIRE(selection.get_nodes() == both);
    tools.press(make_event(
      Point(420, 170), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier));
    REQUIRE(selection.get_nodes() == both);
    tools.release(make_event(
      Point(420, 170), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier));
    REQUIRE(selection.get_nodes() == std::vector{board.m_group});
    click(tools, Point(420, 170), Qt::ShiftModifier);
    REQUIRE(selection.get_nodes() == both);
    drag(tools, Point(420, 170), Point(440, 170), Qt::ShiftModifier);
    REQUIRE(selection.get_nodes() == both);
    REQUIRE(get_bounds(document, board.m_group) == Rect(170, 150, 100, 40));
    REQUIRE(get_bounds(document, board.m_rectangle) ==
      Rect(420, 150, 100, 50));
  }

  TEST_CASE("selected_click") {
    auto document = Document();
    auto board = build_board(document);
    auto other = NodeId();
    {
      auto transaction = Transaction(document);
      auto artboard = Artboard();
      artboard.m_frame = Rect(800, 100, 200, 200);
      other = transaction.append(NodeId(), make_header("Other"), artboard);
      transaction.commit();
      document.clear_history();
    }
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1200, 800));
    auto& selection = editor.get_selection();
    selection.select(board.m_first);
    click(tools, Point(170, 170));
    REQUIRE(selection.get_nodes() == std::vector{board.m_first});
    drag(tools, Point(170, 170), Point(180, 170));
    REQUIRE(get_bounds(document, board.m_first) == Rect(160, 150, 40, 40));
    REQUIRE(get_bounds(document, board.m_second) == Rect(210, 150, 40, 40));
    auto artboards = std::vector{board.m_artboard, other};
    selection.select(artboards);
    drag(tools, Point(420, 170), Point(440, 170));
    REQUIRE(selection.get_nodes() == artboards);
    REQUIRE(document.get(board.m_artboard).get_artboard()->m_frame ==
      Rect(120, 100, 600, 400));
    REQUIRE(document.get(other).get_artboard()->m_frame ==
      Rect(820, 100, 200, 200));
    click(tools, Point(440, 170));
    REQUIRE(selection.get_nodes() == std::vector{board.m_rectangle});
    selection.select(std::vector{board.m_artboard, board.m_rectangle});
    click(tools, Point(180, 160));
    REQUIRE(selection.get_nodes() ==
      std::vector{board.m_artboard, board.m_rectangle});
  }

  TEST_CASE("title") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& titles = tools.get_context().get_titles().get_titles();
    REQUIRE(titles.size() == 1);
    REQUIRE(titles.front().m_artboard == board.m_artboard);
    REQUIRE(titles.front().m_text == "Board");
    REQUIRE(titles.front().m_bounds.m_x == 100);
    REQUIRE(titles.front().m_bounds.get_bottom() ==
      100 - ArtboardTitles::GAP);
    REQUIRE(titles.front().m_bounds.m_width > 0);
    click(tools, Point(102, 90));
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector{board.m_artboard});
    drag(tools, Point(102, 90), Point(152, 100));
    REQUIRE(document.get(board.m_artboard).get_artboard()->m_frame ==
      Rect(150, 110, 600, 400));
    REQUIRE(tools.get_context().get_titles().get_titles().front().m_bounds.
      m_x == 150);
  }

  TEST_CASE("hover") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& context = editor.get_context();
    hover(tools, Point(160, 160));
    REQUIRE(context.get_hover() == board.m_group);
    hover(tools, Point(300, 400));
    REQUIRE(context.get_hover().is_nil());
    hover(tools, Point(220, 160), Qt::ControlModifier);
    REQUIRE(context.get_hover() == board.m_second);
    tools.key_release(KeyEvent(Qt::Key_Control, Qt::NoModifier));
    REQUIRE(context.get_hover() == board.m_group);
    hover(tools, Point(102, 90));
    REQUIRE(context.get_hover() == board.m_artboard);
    hover(tools, Point(420, 320));
    REQUIRE(context.get_hover().is_nil());
    tools.leave();
    REQUIRE(context.get_hover().is_nil());
  }

  TEST_CASE("marquee") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& selection = editor.get_selection();
    drag(tools, Point(120, 120), Point(230, 180));
    REQUIRE(selection.get_nodes() == std::vector{board.m_group});
    drag(tools, Point(600, 120), Point(450, 180), Qt::ShiftModifier);
    REQUIRE(selection.get_nodes() ==
      std::vector{board.m_group, board.m_rectangle});
    drag(tools, Point(380, 280), Point(460, 360));
    REQUIRE(selection.get_nodes() == std::vector{board.m_locked});
    drag(tools, Point(380, 420), Point(460, 480));
    REQUIRE(selection.is_empty());
    drag(tools, Point(50, 50), Point(400, 400));
    REQUIRE(selection.is_empty());
    drag(tools, Point(50, 50), Point(720, 520));
    REQUIRE(selection.get_nodes() == std::vector{board.m_artboard});
    REQUIRE(editor.get_history().get_undo_count() == 0);
  }

  TEST_CASE("marquee_order") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto left = add_rectangle(transaction, NodeId(), Rect(100, 100, 40, 40));
    auto right = add_rectangle(transaction, NodeId(), Rect(300, 100, 40, 40));
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& selection = editor.get_selection();
    drag(tools, Point(90, 90), Point(350, 150));
    REQUIRE(selection.get_nodes() == std::vector{left, right});
    drag(tools, Point(350, 150), Point(90, 90));
    REQUIRE(selection.get_nodes() == std::vector{right, left});
    click(tools, Point(500, 500));
    click(tools, Point(320, 120));
    click(tools, Point(120, 120), Qt::ShiftModifier);
    REQUIRE(selection.get_nodes() == std::vector{right, left});
    click(tools, Point(500, 500));
    click(tools, Point(120, 120));
    click(tools, Point(320, 120), Qt::ShiftModifier);
    REQUIRE(selection.get_nodes() == std::vector{left, right});
  }

  TEST_CASE("move") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& history = editor.get_history();
    drag(tools, Point(160, 160), Point(180.4, 170.6));
    REQUIRE(editor.get_selection().get_nodes() == std::vector{board.m_group});
    REQUIRE(get_bounds(document, board.m_group) == Rect(170, 161, 100, 40));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Move");
    tools.press(make_event(
      Point(180, 170), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(180, 170), Point(220, 175), Qt::ShiftModifier);
    tools.release(make_event(
      Point(220, 175), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier));
    REQUIRE(get_bounds(document, board.m_group) == Rect(210, 161, 100, 40));
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(editor.undo());
    REQUIRE(editor.undo());
    REQUIRE(get_bounds(document, board.m_group) == Rect(150, 150, 100, 40));
    drag(tools, Point(160, 160), Point(161, 161));
    REQUIRE(get_bounds(document, board.m_group) == Rect(150, 150, 100, 40));
    REQUIRE(history.get_undo_count() == 0);
  }

  TEST_CASE("multiple_move") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(make_scale(2, 2), 1, Size(2000, 1600));
    editor.get_selection().select(
      std::vector{board.m_group, board.m_rectangle});
    drag(tools, Point(440, 340), Point(460, 360));
    REQUIRE(editor.get_selection().get_size() == 2);
    REQUIRE(get_bounds(document, board.m_group) == Rect(160, 160, 100, 40));
    REQUIRE(get_bounds(document, board.m_rectangle) ==
      Rect(410, 160, 100, 50));
    REQUIRE(editor.get_history().get_undo_count() == 1);
  }

  TEST_CASE("resize") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& history = editor.get_history();
    editor.get_selection().select(board.m_rectangle);
    drag(tools, Point(500, 200), Point(550, 230));
    require_near(
      get_bounds(document, board.m_rectangle), Rect(400, 150, 150, 80));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Resize");
    REQUIRE(editor.undo());
    drag(tools, Point(500, 200), Point(600, 210), Qt::ShiftModifier);
    require_near(
      get_bounds(document, board.m_rectangle), Rect(400, 150, 200, 100));
    REQUIRE(editor.undo());
    drag(tools, Point(500, 175), Point(520, 175), Qt::AltModifier);
    require_near(
      get_bounds(document, board.m_rectangle), Rect(380, 150, 140, 50));
    REQUIRE(editor.undo());
    drag(tools, Point(400, 150), Point(600, 300));
    require_near(
      get_bounds(document, board.m_rectangle), Rect(500, 200, 100, 100));
    REQUIRE(get_world_transform(
      document, board.m_rectangle).get_determinant() > 0);
    REQUIRE(editor.undo());
    tools.press(make_event(
      Point(500, 200), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(500, 200), Point(600, 210));
    require_near(
      get_bounds(document, board.m_rectangle), Rect(400, 150, 200, 60));
    tools.key_press(KeyEvent(Qt::Key_Shift, Qt::ShiftModifier));
    require_near(
      get_bounds(document, board.m_rectangle), Rect(400, 150, 200, 100));
    tools.release(make_event(
      Point(600, 210), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier));
    REQUIRE(history.get_undo_count() == 1);
  }

  TEST_CASE("clipped_click") {
    auto document = Document();
    auto board = build_board(document);
    auto clipped = NodeId();
    {
      auto transaction = Transaction(document);
      clipped = add_rectangle(
        transaction, board.m_artboard, Rect(550, 350, 200, 40));
      transaction.commit();
      document.clear_history();
    }
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& selection = editor.get_selection();
    click(tools, Point(720, 470));
    REQUIRE(selection.is_empty());
    click(tools, Point(690, 470));
    REQUIRE(selection.get_nodes() == std::vector{clipped});
    click(tools, Point(720, 470));
    REQUIRE(selection.get_nodes() == std::vector{clipped});
    selection.select(board.m_rectangle);
    click(tools, Point(720, 470));
    REQUIRE(selection.get_nodes() == std::vector{clipped});
  }

  TEST_CASE("artboard_resize") {
    auto document = Document();
    auto board = build_board(document);
    auto loose = NodeId();
    {
      auto transaction = Transaction(document);
      loose = add_rectangle(transaction, NodeId(), Rect(720, 150, 40, 40));
      transaction.commit();
      document.clear_history();
    }
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(board.m_artboard);
    tools.press(make_event(
      Point(700, 300), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(700, 300), Point(740, 300));
    REQUIRE(document.get(board.m_artboard).get_artboard()->m_frame ==
      Rect(100, 100, 640, 400));
    REQUIRE(document.get(loose).m_parent.is_nil());
    tools.release(make_event(
      Point(740, 300), Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    REQUIRE(document.get(loose).m_parent == board.m_artboard);
    REQUIRE(document.get(board.m_artboard).m_children.back() == loose);
    REQUIRE(get_bounds(document, loose) == Rect(720, 150, 40, 40));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.undo());
    REQUIRE(document.get(loose).m_parent.is_nil());
    REQUIRE(document.get(board.m_artboard).get_artboard()->m_frame ==
      Rect(100, 100, 600, 400));
  }

  TEST_CASE("rotate") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& history = editor.get_history();
    editor.get_selection().select(board.m_rectangle);
    hover(tools, Point(505, 145));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    hover(tools, Point(501, 151));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RESIZE);
    hover(tools, Point(450, 175));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    drag(tools, Point(505, 145), Point(480, 230));
    auto transform = get_world_transform(document, board.m_rectangle);
    REQUIRE(std::atan2(transform.m_b, transform.m_a) ==
      doctest::Approx(std::numbers::pi / 2));
    auto bounds = get_bounds(document, board.m_rectangle);
    REQUIRE(bounds.m_x == doctest::Approx(425));
    REQUIRE(bounds.m_y == doctest::Approx(125));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Rotate");
    REQUIRE(editor.undo());
    auto angle = 50 * std::numbers::pi / 180;
    auto start = Point(55, -30);
    auto end = Point(start.m_x * std::cos(angle) - start.m_y * std::sin(angle),
      start.m_x * std::sin(angle) + start.m_y * std::cos(angle));
    drag(tools, Point(505, 145), Point(450, 175) + end, Qt::ShiftModifier);
    transform = get_world_transform(document, board.m_rectangle);
    REQUIRE(std::atan2(transform.m_b, transform.m_a) ==
      doctest::Approx(std::numbers::pi / 4));
  }

  TEST_CASE("nudge") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& history = editor.get_history();
    REQUIRE(!tools.key_press(KeyEvent(Qt::Key_Right, Qt::NoModifier)));
    editor.get_selection().select(board.m_rectangle);
    type(tools, Qt::Key_Right);
    REQUIRE(get_bounds(document, board.m_rectangle).m_x == 401);
    type(tools, Qt::Key_Right, Qt::ShiftModifier);
    REQUIRE(get_bounds(document, board.m_rectangle).m_x == 411);
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(history.get_undo_label() == "Nudge");
    tools.key_press(KeyEvent(Qt::Key_Down, Qt::NoModifier));
    type(tools, Qt::Key_Down, Qt::NoModifier, true);
    type(tools, Qt::Key_Down, Qt::NoModifier, true);
    tools.key_release(KeyEvent(Qt::Key_Down, Qt::NoModifier));
    REQUIRE(get_bounds(document, board.m_rectangle).m_y == 153);
    REQUIRE(history.get_undo_count() == 3);
    type(tools, Qt::Key_Up);
    REQUIRE(history.get_undo_count() == 4);
    editor.get_selection().select(board.m_locked);
    REQUIRE(!tools.key_press(KeyEvent(Qt::Key_Up, Qt::NoModifier)));
  }

  TEST_CASE("cancel") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.press(make_event(
      Point(160, 160), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(160, 160), Point(200, 200));
    REQUIRE(tools.is_busy());
    REQUIRE(get_bounds(document, board.m_group) == Rect(190, 190, 100, 40));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Escape, Qt::NoModifier)));
    REQUIRE(!tools.is_busy());
    REQUIRE(get_bounds(document, board.m_group) == Rect(150, 150, 100, 40));
    REQUIRE(editor.get_history().get_undo_count() == 0);
    REQUIRE(editor.get_selection().get_nodes() == std::vector{board.m_group});
    tools.release(make_event(
      Point(200, 200), Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    REQUIRE(get_bounds(document, board.m_group) == Rect(150, 150, 100, 40));
  }

  TEST_CASE("corner_radius") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto get_radii = [&] {
      return std::get<RectangleShape>(
        document.get(board.m_rectangle).get_shape()->m_geometry).m_radii;
    };
    click(tools, Point(420, 170));
    drag(tools, Point(414, 164), Point(424, 174));
    REQUIRE(get_radii() == CornerRadii(10, 10, 10, 10));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.get_history().get_undo_label() == "Corner Radius");
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector{board.m_rectangle});
    drag(tools, Point(486, 186), Point(476, 176), Qt::AltModifier);
    REQUIRE(get_radii() == CornerRadii(10, 10, 20, 10));
    REQUIRE(editor.get_history().get_undo_count() == 2);
    drag(tools, Point(410, 160), Point(300, 100));
    REQUIRE(get_radii() == CornerRadii(0, 0, 0, 0));
    drag(tools, Point(414, 164), Point(600, 400));
    REQUIRE(get_radii() == CornerRadii(25, 25, 25, 25));
    tools.press(make_event(
      Point(425, 175), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(425, 175), Point(415, 165));
    REQUIRE(get_radii() == CornerRadii(15, 15, 15, 15));
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Escape, Qt::NoModifier)));
    REQUIRE(get_radii() == CornerRadii(25, 25, 25, 25));
    editor.get_selection().select(board.m_locked);
    REQUIRE(get_handle_centers(tools.draw()).empty());
  }

  TEST_CASE("copy_drag") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(board.m_rectangle);
    drag(tools, Point(450, 175), Point(470, 185), Qt::AltModifier);
    auto& nodes = editor.get_selection().get_nodes();
    REQUIRE(nodes.size() == 1);
    auto copy = nodes.front();
    REQUIRE(copy != board.m_rectangle);
    REQUIRE(get_bounds(document, copy) == Rect(420, 160, 100, 50));
    REQUIRE(get_bounds(document, board.m_rectangle) ==
      Rect(400, 150, 100, 50));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.undo());
    REQUIRE(!document.find(copy));
    tools.press(make_event(
      Point(450, 175), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(450, 175), Point(460, 175));
    REQUIRE(get_bounds(document, board.m_rectangle) ==
      Rect(410, 150, 100, 50));
    tools.key_press(KeyEvent(Qt::Key_Alt, Qt::AltModifier));
    REQUIRE(get_bounds(document, board.m_rectangle) ==
      Rect(400, 150, 100, 50));
    drag_to(tools, Point(460, 175), Point(480, 185), Qt::AltModifier);
    tools.release(make_event(
      Point(480, 185), Qt::LeftButton, Qt::NoButton, Qt::AltModifier));
    REQUIRE(nodes.size() == 1);
    copy = nodes.front();
    REQUIRE(copy != board.m_rectangle);
    REQUIRE(get_bounds(document, copy) == Rect(430, 160, 100, 50));
    REQUIRE(get_bounds(document, board.m_rectangle) ==
      Rect(400, 150, 100, 50));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.undo());
    REQUIRE(!document.find(copy));
  }

  TEST_CASE("flip") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(board.m_rectangle);
    tools.press(make_event(
      Point(500, 175), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(500, 175), Point(350, 175));
    require_near(
      get_bounds(document, board.m_rectangle), Rect(350, 150, 50, 50));
    drag_to(tools, Point(350, 175), Point(450, 175));
    require_near(
      get_bounds(document, board.m_rectangle), Rect(400, 150, 50, 50));
    tools.release(make_event(
      Point(450, 175), Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(get_world_transform(
      document, board.m_rectangle).get_determinant() > 0);
  }

  TEST_CASE("locked") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    click(tools, Point(420, 320));
    REQUIRE(editor.get_selection().get_nodes() == std::vector{board.m_locked});
    drag(tools, Point(450, 350), Point(480, 380));
    REQUIRE(get_bounds(document, board.m_locked) == Rect(400, 300, 50, 50));
    REQUIRE(editor.get_history().get_undo_count() == 0);
    hover(tools, Point(451, 351));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    auto overlay = tools.draw();
    REQUIRE(get_handle_centers(overlay).empty());
    auto handles = std::size_t(0);
    for(auto& shape : overlay.m_shapes) {
      REQUIRE(shape.m_fill.m_red == shape.m_fill.m_green);
      REQUIRE(shape.m_fill.m_red == shape.m_fill.m_blue);
      REQUIRE(shape.m_fill.m_red < 1);
      if(shape.m_radius == SelectionHandles::LOCKED_HANDLE_RADIUS) {
        ++handles;
      } else {
        REQUIRE(shape.m_radius == 0);
      }
    }
    REQUIRE(handles == 8);
    REQUIRE(overlay.m_images.size() == 2);
    auto& badge = overlay.m_images.back();
    REQUIRE(badge.m_image->width() ==
      static_cast<int>(SelectionHandles::LOCK_BADGE_SIZE));
    REQUIRE(badge.m_position.m_x + SelectionHandles::LOCK_BADGE_SIZE < 400);
    REQUIRE(badge.m_position.m_y + SelectionHandles::LOCK_BADGE_SIZE < 300);
    tools.double_click(make_event(
      Point(420, 320), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    REQUIRE(tools.get_tool_name() == "select");
    REQUIRE(editor.get_selection().get_nodes() == std::vector{board.m_locked});
    click(tools, Point(450, 175));
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector{board.m_rectangle});
    REQUIRE(tools.get_context().get_hit_tester().hit(
      Point(420, 320), 0) == board.m_artboard);
  }

  TEST_CASE("hover_outline") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto ellipse = transaction.append(NodeId(),
      make_header("Ellipse", make_translation(100, 100)),
      Shape(EllipseShape(Point(50, 25), 50, 25), make_default_style()));
    transaction.commit();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    hover(tools, Point(150, 125));
    REQUIRE(editor.get_context().get_hover() == ellipse);
    auto overlay = tools.draw();
    REQUIRE(overlay.m_shapes.size() > 16);
    for(auto& shape : overlay.m_shapes) {
      auto offset = shape.m_center - Point(150, 125);
      auto value = (offset.m_x * offset.m_x) / (50 * 50) +
        (offset.m_y * offset.m_y) / (25 * 25);
      REQUIRE(value == doctest::Approx(1).epsilon(0.05));
    }
  }

  TEST_CASE("overlay") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto overlay = tools.draw();
    REQUIRE(overlay.m_shapes.empty());
    REQUIRE(overlay.m_images.size() == 1);
    require_near(overlay.m_images.front().m_position,
      Point(100, 100 - ArtboardTitles::GAP - ArtboardTitles::HEIGHT));
    editor.get_selection().select(board.m_rectangle);
    overlay = tools.draw();
    auto centers = get_handle_centers(overlay);
    REQUIRE(centers.size() == 12);
    require_near(centers[0], Point(400, 150));
    require_near(centers[1], Point(450, 150));
    require_near(centers[4], Point(500, 200));
    require_near(centers[8], Point(414, 164));
    require_near(centers[10], Point(486, 186));
    REQUIRE(overlay.m_shapes.size() == 20);
    require_sizes(overlay);
    tools.set_view(make_translation(10, 0) * make_scale(2, 2), 2,
      Size(2000, 1600));
    overlay = tools.draw();
    centers = get_handle_centers(overlay);
    require_near(centers[0], Point(810, 300));
    require_near(centers[4], Point(1010, 400));
    require_near(centers[8], Point(824, 314));
    require_near(overlay.m_images.front().m_position,
      Point(210, 200 - ArtboardTitles::GAP - ArtboardTitles::HEIGHT));
    REQUIRE(overlay.m_images.front().m_image->height() ==
      2 * ArtboardTitles::HEIGHT);
    require_sizes(overlay);
    tools.set_view(make_scale(0.1, 0.1), 1, Size(100, 80));
    overlay = tools.draw();
    REQUIRE(get_handle_centers(overlay).size() == 3);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    hover(tools, Point(160, 160));
    overlay = tools.draw();
    REQUIRE(overlay.m_shapes.size() == 24);
    tools.press(make_event(
      Point(160, 160), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(160, 160), Point(170, 160));
    overlay = tools.draw();
    REQUIRE(get_handle_centers(overlay).empty());
    REQUIRE(overlay.m_shapes.size() == 4);
    tools.release(make_event(
      Point(170, 160), Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    tools.press(make_event(
      Point(650, 450), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(650, 450), Point(600, 420));
    overlay = tools.draw();
    REQUIRE(overlay.m_shapes.size() == 1);
    require_near(overlay.m_shapes.front().m_center, Point(625, 435));
    REQUIRE(overlay.m_shapes.front().m_size == Size(50, 30));
  }

  TEST_CASE("stack_drag") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_stack = Stack(Orientation::VERTICAL);
    auto stack = transaction.append(
      NodeId(), make_header("Stack", make_translation(100, 100)), group);
    auto last = add_rectangle(transaction, stack, Rect(0, 90, 100, 20));
    auto middle = add_rectangle(transaction, stack, Rect(0, 50, 100, 30));
    auto first = add_rectangle(transaction, stack, Rect(0, 0, 100, 40));
    auto payload = *document.get(stack).get_group();
    payload.m_stack->m_spacing.emplace(first, 10);
    payload.m_stack->m_spacing.emplace(middle, 10);
    transaction.replace_payload(stack, payload);
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_context().enter(stack);
    click(tools, Point(150, 120));
    REQUIRE(editor.get_selection().get_nodes() == std::vector{first});
    tools.press(make_event(
      Point(150, 120), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(150, 120), Point(150, 190));
    REQUIRE(get_bounds(document, middle).m_y == 100);
    REQUIRE(get_bounds(document, first).m_y == 170);
    tools.release(make_event(
      Point(150, 190), Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    REQUIRE(document.get(stack).m_children ==
      std::vector{last, first, middle});
    REQUIRE(get_bounds(document, first).m_y == 140);
    REQUIRE(get_bounds(document, last).m_y == 190);
    REQUIRE(editor.get_history().get_undo_count() == 1);
    editor.undo();
    REQUIRE(get_bounds(document, first).m_y == 100);
  }

  TEST_CASE("stack_nudge") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_stack = Stack(Orientation::HORIZONTAL);
    auto stack = transaction.append(
      NodeId(), make_header("Stack", make_translation(100, 100)), group);
    auto last = add_rectangle(transaction, stack, Rect(50, 0, 40, 30));
    auto first = add_rectangle(transaction, stack, Rect(0, 0, 40, 30));
    auto payload = *document.get(stack).get_group();
    payload.m_stack->m_spacing.emplace(first, 10);
    transaction.replace_payload(stack, payload);
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(first);
    for(auto key : {Qt::Key_Left, Qt::Key_Right}) {
      for(auto modifiers : {Qt::NoModifier, Qt::ShiftModifier}) {
        REQUIRE(tools.key_press(KeyEvent(key, modifiers)));
        tools.key_release(KeyEvent(key, modifiers));
      }
    }
    REQUIRE(document.get(stack).m_children == std::vector{last, first});
    REQUIRE(get_bounds(document, first) == Rect(100, 100, 40, 30));
    REQUIRE(editor.get_history().get_undo_count() == 0);
    type(tools, Qt::Key_Down);
    REQUIRE(get_bounds(document, first) == Rect(100, 101, 40, 30));
    type(tools, Qt::Key_Up, Qt::ShiftModifier);
    REQUIRE(get_bounds(document, first) == Rect(100, 91, 40, 30));
    REQUIRE(get_bounds(document, last) == Rect(150, 100, 40, 30));
    REQUIRE(editor.get_history().get_undo_count() == 2);
    editor.get_selection().select(std::vector{first, last});
    type(tools, Qt::Key_Right);
    REQUIRE(get_bounds(document, first).m_x == 100);
    REQUIRE(get_bounds(document, last).m_x == 150);
  }

  TEST_CASE("responsive_resize") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& history = editor.get_history();
    editor.get_selection().select(board.m_group);
    drag(tools, Point(250, 170), Point(350, 170));
    require_near(get_bounds(document, board.m_first), Rect(150, 150, 40, 40));
    require_near(
      get_bounds(document, board.m_second), Rect(310, 150, 40, 40));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(document.get(board.m_second).m_header.m_constraints.m_right);
    REQUIRE(editor.undo());
    REQUIRE(!document.get(board.m_second).m_header.m_constraints.m_right);
    drag(tools, Point(250, 170), Point(350, 170), Qt::ShiftModifier);
    require_near(get_bounds(document, board.m_first), Rect(150, 150, 80, 40));
    require_near(
      get_bounds(document, board.m_second), Rect(270, 150, 80, 40));
    REQUIRE(editor.undo());
    tools.press(make_event(
      Point(250, 170), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(250, 170), Point(160, 170));
    drag_to(tools, Point(160, 170), Point(300, 170));
    tools.release(make_event(
      Point(300, 170), Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    require_near(
      get_bounds(document, board.m_second), Rect(260, 150, 40, 40));
    REQUIRE(history.get_undo_count() == 1);
  }

  TEST_CASE("stack_handles") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_stack = Stack(Orientation::HORIZONTAL);
    auto row = transaction.append(
      NodeId(), make_header("Row", make_translation(100, 100)), group);
    auto last = add_rectangle(transaction, row, Rect(40, 0, 50, 60));
    auto first = add_rectangle(transaction, row, Rect(0, 0, 40, 60));
    group.m_stack = Stack(Orientation::VERTICAL);
    auto column = transaction.append(
      NodeId(), make_header("Column", make_translation(300, 100)), group);
    add_rectangle(transaction, column, Rect(0, 50, 60, 40));
    add_rectangle(transaction, column, Rect(0, 0, 60, 40));
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& history = editor.get_history();
    editor.get_selection().select(row);
    auto centers = get_handle_centers(tools.draw());
    REQUIRE(centers.size() == 2);
    require_near(centers[0], Point(145, 100));
    require_near(centers[1], Point(145, 160));
    hover(tools, Point(145, 161));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RESIZE);
    for(auto point : {Point(190, 130), Point(100, 130), Point(101, 101),
        Point(189, 159)}) {
      hover(tools, point);
      REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    }
    drag(tools, Point(190, 130), Point(145, 200));
    REQUIRE(history.get_undo_label() == "Rotate");
    require_near(*get_local_bounds(document, row), Rect(0, 0, 90, 60));
    REQUIRE(editor.undo());
    drag(tools, Point(145, 160), Point(145, 180));
    REQUIRE(history.get_undo_label() == "Resize");
    REQUIRE(get_bounds(document, row).m_width == doctest::Approx(90));
    REQUIRE(editor.undo());
    editor.get_selection().select(column);
    centers = get_handle_centers(tools.draw());
    REQUIRE(centers.size() == 2);
    require_near(centers[0], Point(360, 145));
    require_near(centers[1], Point(300, 145));
    hover(tools, Point(330, 100));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    editor.get_selection().select(std::vector{first, last});
    centers = get_handle_centers(tools.draw());
    REQUIRE(centers.size() == 2);
    require_near(centers[0], Point(145, 100));
    require_near(centers[1], Point(145, 160));
    for(auto point : {Point(190, 130), Point(101, 101), Point(95, 95)}) {
      hover(tools, point);
      REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    }
    drag(tools, Point(95, 95), Point(80, 80));
    REQUIRE(get_bounds(document, row) == Rect(100, 100, 90, 60));
    REQUIRE(history.get_undo_count() == 0);
  }

  TEST_CASE("text_handles") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto payload = make_text(u"Label");
    payload.m_frame = PositionedFrame();
    auto point = transaction.append(
      NodeId(), make_header("Point", make_translation(100, 100)), payload);
    payload.m_frame = AutoHeightFrame(100);
    auto automatic = transaction.append(
      NodeId(), make_header("Auto", make_translation(300, 100)), payload);
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(point);
    auto bounds = get_bounds(document, point);
    auto bottom = Point(bounds.get_center().m_x, bounds.get_bottom());
    auto centers = get_handle_centers(tools.draw());
    REQUIRE(centers.size() == 1);
    require_near(centers[0], bottom);
    hover(tools, bounds.get_bottom_right());
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    hover(tools, bottom + Point(0, 8));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    hover(tools, bottom + Point(0, 10));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    hover(tools, bounds.get_top_left() - Point(5, 5));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    drag(tools, bottom, bottom + Point(0, bounds.m_height));
    auto resized = get_bounds(document, point);
    REQUIRE(resized.m_width > 1.5 * bounds.m_width);
    REQUIRE(resized.m_height > 1.5 * bounds.m_height);
    REQUIRE(document.get(point).get_text()->m_base_style.m_font.m_size >
      payload.m_base_style.m_font.m_size);
    editor.get_selection().select(automatic);
    bounds = get_bounds(document, automatic);
    centers = get_handle_centers(tools.draw());
    REQUIRE(centers.size() == 4);
    hover(tools, bounds.get_top_left());
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    hover(tools, Point(bounds.get_right(), bounds.get_center().m_y));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RESIZE);
    auto& history = editor.get_history();
    auto get_frame = [&] () -> const auto& {
      return document.get(automatic).get_text()->m_frame;
    };
    auto middle = bounds.get_center().m_x;
    drag(tools, Point(middle, bounds.get_bottom()),
      Point(middle, bounds.get_bottom() + 30));
    auto area = std::get_if<AreaFrame>(&get_frame());
    REQUIRE(area);
    REQUIRE(area->m_size.m_width == 100);
    REQUIRE(std::abs(area->m_size.m_height - bounds.m_height - 30) <= 1);
    REQUIRE(get_world_transform(document, automatic) ==
      make_translation(300, 100));
    REQUIRE(history.get_undo_label() == "Resize");
    REQUIRE(editor.undo());
    REQUIRE(std::holds_alternative<AutoHeightFrame>(get_frame()));
    auto right = Point(bounds.get_right(), bounds.get_center().m_y);
    drag(tools, right, right + Point(20, 0));
    auto width = std::get_if<AutoHeightFrame>(&get_frame());
    REQUIRE(width);
    REQUIRE(width->m_width == 120);
  }

  TEST_CASE("rotation_zones") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(board.m_rectangle);
    hover(tools, Point(450, 145));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RESIZE);
    hover(tools, Point(450, 141));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    hover(tools, Point(450, 140));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    hover(tools, Point(509, 175));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    hover(tools, Point(507, 143));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    hover(tools, Point(508, 142));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    drag(tools, Point(450, 141), Point(530, 175));
    auto transform = get_world_transform(document, board.m_rectangle);
    REQUIRE(std::atan2(transform.m_b, transform.m_a) ==
      doctest::Approx(std::numbers::pi / 2));
    REQUIRE(editor.get_history().get_undo_label() == "Rotate");
    REQUIRE(editor.get_history().get_undo_count() == 1);
  }

  TEST_CASE("repeat_grid_rotation") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_repeat_grid =
      RepeatGrid(Size(40, 40), 20, 20, 2, 1, Size(100, 40));
    auto grid = transaction.append(
      NodeId(), make_header("Grid", make_translation(100, 100)), group);
    auto cell = transaction.append(grid, make_header(""), Group());
    add_rectangle(transaction, cell, Rect(0, 0, 40, 40));
    auto second = transaction.append(
      grid, make_header("", make_translation(60, 0)), Group());
    add_rectangle(transaction, second, Rect(0, 0, 40, 40));
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(grid);
    REQUIRE(get_handle_centers(tools.draw()).empty());
    hover(tools, Point(208, 120));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    hover(tools, Point(200, 120));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RESIZE);
    hover(tools, Point(150, 146));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ROTATE);
    hover(tools, Point(97, 97));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    hover(tools, Point(202, 97));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    drag(tools, Point(150, 146), Point(90, 120));
    auto transform = get_world_transform(document, grid);
    REQUIRE(std::atan2(transform.m_b, transform.m_a) ==
      doctest::Approx(std::numbers::pi / 2));
    REQUIRE(editor.get_history().get_undo_label() == "Rotate Repeat Grid");
    REQUIRE(editor.get_history().get_undo_count() == 1);
  }

  TEST_CASE("line_endpoints") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto line = transaction.append(NodeId(),
      make_header("Line", make_translation(300, 300)),
      Shape(LineShape(Point(0, 0), Point(100, 50)), make_default_line_style()));
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& history = editor.get_history();
    auto get_points = [&] {
      auto& shape = std::get<LineShape>(
        document.get(line).get_shape()->m_geometry);
      auto transform = get_world_transform(document, line);
      return std::vector{
        transform.map(shape.m_start), transform.map(shape.m_end)};
    };
    editor.get_selection().select(line);
    auto overlay = tools.draw();
    REQUIRE(overlay.m_shapes.size() == 2);
    auto centers = get_handle_centers(overlay);
    REQUIRE(centers == std::vector{Point(300, 300), Point(400, 350)});
    hover(tools, Point(400, 300));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    drag(tools, Point(400, 350), Point(420, 330));
    REQUIRE(get_points() == std::vector{Point(300, 300), Point(420, 330)});
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Drag Handle");
    REQUIRE(editor.get_selection().get_nodes() == std::vector{line});
    REQUIRE(editor.undo());
    drag(tools, Point(400, 350), Point(450, 310), Qt::ShiftModifier);
    REQUIRE(get_points() == std::vector{Point(300, 300), Point(450, 300)});
    REQUIRE(editor.undo());
    drag(tools, Point(400, 350), Point(450, 390), Qt::ShiftModifier);
    REQUIRE(get_points() == std::vector{Point(300, 300), Point(424, 424)});
    REQUIRE(editor.undo());
    drag(tools, Point(300, 300), Point(310, 320), Qt::AltModifier);
    REQUIRE(get_points() == std::vector{Point(310, 320), Point(390, 330)});
    REQUIRE(history.get_undo_count() == 1);
    tools.press(make_event(
      Point(310, 320), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    drag_to(tools, Point(310, 320), Point(250, 250));
    REQUIRE(get_points() == std::vector{Point(250, 250), Point(390, 330)});
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_Escape, Qt::NoModifier)));
    REQUIRE(get_points() == std::vector{Point(310, 320), Point(390, 330)});
    REQUIRE(history.get_undo_count() == 1);
  }

  TEST_CASE("scroll_group_handles") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_scroll_group =
      ScrollGroup(ScrollDirection::PANNING, Size(20, 20), Point(0, 0));
    auto scroll = transaction.append(
      NodeId(), make_header("Scroll", make_translation(100, 100)), group);
    add_rectangle(transaction, scroll, Rect(0, 0, 20, 20));
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto get_pills = [&] {
      auto pills = std::vector<OverlayShape>();
      for(auto& shape : tools.draw().m_shapes) {
        if(shape.m_radius == SelectionHandles::SCROLL_HANDLE_RADIUS) {
          pills.push_back(shape);
        }
      }
      return pills;
    };
    editor.get_selection().select(scroll);
    auto pills = get_pills();
    REQUIRE(pills.size() == 2);
    require_near(pills[0].m_center, Point(119.5, 110));
    REQUIRE(pills[0].m_size == Size(10, 18));
    require_near(pills[1].m_center, Point(100.5, 110));
    REQUIRE(get_handle_centers(tools.draw()).empty());
    REQUIRE(tools.draw().m_shapes.size() == 4 + 2 + 4);
    hover(tools, Point(97, 97));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
    tools.set_view(make_scale(2, 2), 1, Size(1000, 800));
    REQUIRE(get_pills().size() == 4);
    hover(tools, Point(228, 200));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RESIZE);
    click(tools, Point(220, 240));
    REQUIRE(editor.get_history().get_undo_count() == 0);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    pills = get_pills();
    REQUIRE(pills.size() == 2);
    require_near(pills[0].m_center, Point(110, 100.5));
    REQUIRE(pills[0].m_size == Size(18, 10));
    require_near(pills[1].m_center, Point(110, 119.5));
    tools.set_view(make_scale(0.25, 0.25), 1, Size(1000, 800));
    auto overlay = tools.draw();
    for(auto& shape : overlay.m_shapes) {
      if(shape.m_radius > 0) {
        REQUIRE((shape.m_size == Size(14, 8) || shape.m_size == Size(8, 14)));
      } else {
        REQUIRE(shape.m_size.m_height == 1);
      }
    }
    REQUIRE(overlay.m_shapes.size() == 4 + get_pills().size());
  }
}
