#include <doctest/doctest.h>
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Tools/LayoutHandles.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Board {
    NodeId m_grid;
    NodeId m_cell;
    NodeId m_rectangle;
  };

  Board build_board(Document& document) {
    auto board = Board();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_repeat_grid =
      RepeatGrid(Size(40, 40), 20, 20, 2, 1, Size(100, 40));
    board.m_grid = transaction.append(
      NodeId(), make_header("Grid", make_translation(100, 100)), group);
    board.m_cell = transaction.append(board.m_grid, make_header(""), Group());
    board.m_rectangle = transaction.append(board.m_cell,
      make_header("Rectangle"), make_rectangle_shape(40, 40));
    auto second = transaction.append(board.m_grid,
      make_header("", make_translation(60, 0)), Group());
    transaction.append(
      second, make_header("Rectangle"), make_rectangle_shape(40, 40));
    transaction.commit();
    document.clear_history();
    return board;
  }

  PointerEvent make_event(
      Point position, Qt::MouseButton button, Qt::MouseButtons buttons) {
    return PointerEvent(position, button, buttons, Qt::KeyboardModifiers());
  }

  void drag(ToolManager& tools, Point from, Point to) {
    tools.press(make_event(from, Qt::LeftButton, Qt::LeftButton));
    for(auto i = 1; i <= 4; ++i) {
      tools.move(make_event(
        interpolate(from, to, i / 4.0), Qt::NoButton, Qt::LeftButton));
    }
    tools.release(make_event(to, Qt::LeftButton, Qt::NoButton));
  }

  const RepeatGrid& get_grid(const Document& document, const NodeId& id) {
    return *document.get(id).get_group()->m_repeat_grid;
  }
}

TEST_SUITE("LayoutHandles") {
  TEST_CASE("selection") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto handles = LayoutHandles();
    REQUIRE(handles.get_grid(tools.get_context()).is_nil());
    editor.get_selection().select(board.m_grid);
    REQUIRE(handles.get_grid(tools.get_context()) == board.m_grid);
    auto overlay = CanvasOverlay();
    handles.draw(tools.get_context(), overlay);
    REQUIRE(!overlay.m_shapes.empty());
    REQUIRE(handles.hover(tools.get_context(), Point(200, 120)));
    REQUIRE(handles.hover(tools.get_context(), Point(150, 120)));
    REQUIRE(!handles.hover(tools.get_context(), Point(120, 120)));
    editor.get_selection().select(board.m_rectangle);
    REQUIRE(handles.get_grid(tools.get_context()).is_nil());
  }

  TEST_CASE("columns") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(board.m_grid);
    drag(tools, Point(200, 120), Point(260, 120));
    REQUIRE(get_grid(document, board.m_grid).m_size == Size(160, 40));
    REQUIRE(get_grid(document, board.m_grid).m_column_count == 3);
    REQUIRE(document.get(board.m_grid).m_children.size() == 3);
    REQUIRE(editor.get_history().get_undo_count() == 1);
    drag(tools, Point(180, 140), Point(180, 200));
    REQUIRE(get_grid(document, board.m_grid).m_row_count == 2);
    REQUIRE(document.get(board.m_grid).m_children.size() == 6);
    REQUIRE(editor.get_history().get_undo_count() == 2);
    editor.undo();
    REQUIRE(document.get(board.m_grid).m_children.size() == 3);
  }

  TEST_CASE("spacing") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(board.m_grid);
    drag(tools, Point(150, 120), Point(160, 120));
    REQUIRE(get_grid(document, board.m_grid).m_column_spacing == 30);
    REQUIRE(get_grid(document, board.m_grid).m_size == Size(110, 40));
    auto second = document.get(board.m_grid).m_children.back();
    REQUIRE(get_transform(document, second) == make_translation(70, 0));
    REQUIRE(editor.get_history().get_undo_count() == 1);
  }

  TEST_CASE("cancel") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(board.m_grid);
    tools.press(make_event(Point(200, 120), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(270, 120), Qt::NoButton, Qt::LeftButton));
    REQUIRE(get_grid(document, board.m_grid).m_column_count == 3);
    REQUIRE(tools.is_busy());
    tools.cancel();
    REQUIRE(!tools.is_busy());
    REQUIRE(get_grid(document, board.m_grid).m_size == Size(100, 40));
    REQUIRE(document.get(board.m_grid).m_children.size() == 2);
    REQUIRE(!editor.get_history().can_undo());
  }

  TEST_CASE("cell_entry") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.double_click(
      make_event(Point(120, 120), Qt::LeftButton, Qt::LeftButton));
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector{board.m_rectangle});
    REQUIRE(editor.get_context().get_container() == board.m_cell);
  }

  TEST_CASE("stack_gap") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_stack = Stack(Orientation::VERTICAL);
    auto stack = transaction.append(
      NodeId(), make_header("Stack", make_translation(100, 100)), group);
    auto last = transaction.append(stack,
      make_header("Last", make_translation(0, 50)),
      make_rectangle_shape(100, 20));
    auto first = transaction.append(
      stack, make_header("First"), make_rectangle_shape(100, 40));
    auto payload = *document.get(stack).get_group();
    payload.m_stack->m_spacing.emplace(first, 10);
    transaction.replace_payload(stack, payload);
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(stack);
    auto handles = LayoutHandles();
    REQUIRE(handles.get_stack(tools.get_context()) == stack);
    REQUIRE(handles.get_grid(tools.get_context()).is_nil());
    REQUIRE(handles.hover(tools.get_context(), Point(150, 145)));
    REQUIRE(!handles.hover(tools.get_context(), Point(150, 120)));
    drag(tools, Point(150, 145), Point(150, 155));
    REQUIRE(get_transform(document, last) == make_translation(0, 60));
    REQUIRE(document.get(stack).get_group()->m_stack->m_spacing.at(first) ==
      20);
    REQUIRE(editor.get_history().get_undo_count() == 1);
  }
}
