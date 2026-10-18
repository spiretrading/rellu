#include <cmath>
#include <numbers>
#include <doctest/doctest.h>
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
      if(shape.m_radius > 0) {
        centers.push_back(shape.m_center);
      }
    }
    return centers;
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
    REQUIRE(selection.is_empty());
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
    click(tools, Point(160, 160));
    click(tools, Point(420, 170), Qt::ShiftModifier);
    REQUIRE(selection.get_nodes() ==
      std::vector{board.m_group, board.m_rectangle});
    click(tools, Point(160, 160));
    REQUIRE(selection.get_nodes() == std::vector{board.m_group});
    click(tools, Point(420, 170), Qt::ShiftModifier);
    click(tools, Point(420, 170), Qt::ShiftModifier);
    REQUIRE(selection.get_nodes() == std::vector{board.m_group});
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
    REQUIRE(selection.is_empty());
    drag(tools, Point(50, 50), Point(400, 400));
    REQUIRE(selection.is_empty());
    drag(tools, Point(50, 50), Point(720, 520));
    REQUIRE(selection.get_nodes() == std::vector{board.m_artboard});
    REQUIRE(editor.get_history().get_undo_count() == 0);
  }

  TEST_CASE("move") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
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
      get_bounds(document, board.m_rectangle), Rect(499, 199, 1, 1));
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

  TEST_CASE("locked") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(board.m_locked);
    drag(tools, Point(450, 350), Point(480, 380));
    REQUIRE(get_bounds(document, board.m_locked) == Rect(400, 300, 50, 50));
    REQUIRE(editor.get_history().get_undo_count() == 0);
    hover(tools, Point(451, 351));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARROW);
  }

  TEST_CASE("overlay") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto overlay = tools.draw();
    REQUIRE(overlay.m_shapes.empty());
    REQUIRE(overlay.m_images.size() == 1);
    require_near(overlay.m_images.front().m_position,
      Point(100, 100 - ArtboardTitles::GAP - ArtboardTitles::HEIGHT));
    editor.get_selection().select(board.m_rectangle);
    overlay = tools.draw();
    auto centers = get_handle_centers(overlay);
    REQUIRE(centers.size() == 8);
    require_near(centers[0], Point(400, 150));
    require_near(centers[1], Point(450, 150));
    require_near(centers[4], Point(500, 200));
    REQUIRE(overlay.m_shapes.size() == 12);
    for(auto& shape : overlay.m_shapes) {
      if(shape.m_radius > 0) {
        REQUIRE(shape.m_size == Size(2 * SelectTool::HANDLE_RADIUS,
          2 * SelectTool::HANDLE_RADIUS));
      } else {
        REQUIRE(shape.m_size.m_height == 1);
      }
    }
    tools.set_view(make_translation(10, 0) * make_scale(2, 2), 2,
      Size(2000, 1600));
    overlay = tools.draw();
    centers = get_handle_centers(overlay);
    require_near(centers[0], Point(810, 300));
    require_near(centers[4], Point(1010, 400));
    require_near(overlay.m_images.front().m_position,
      Point(210, 200 - ArtboardTitles::GAP - ArtboardTitles::HEIGHT));
    REQUIRE(overlay.m_images.front().m_image->height() ==
      2 * ArtboardTitles::HEIGHT);
    for(auto& shape : overlay.m_shapes) {
      if(shape.m_radius > 0) {
        REQUIRE(shape.m_size == Size(2 * SelectTool::HANDLE_RADIUS,
          2 * SelectTool::HANDLE_RADIUS));
      } else {
        REQUIRE(shape.m_size.m_height == 1);
      }
    }
    tools.set_view(make_scale(0.1, 0.1), 1, Size(100, 80));
    overlay = tools.draw();
    REQUIRE(get_handle_centers(overlay).size() == 4);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    hover(tools, Point(160, 160));
    overlay = tools.draw();
    REQUIRE(overlay.m_shapes.size() == 16);
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
}
