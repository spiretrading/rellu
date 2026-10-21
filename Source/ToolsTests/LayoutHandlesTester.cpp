#include <tuple>
#include <utility>
#include <vector>
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

  TEST_CASE("grid_handles") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(board.m_grid);
    auto handles = LayoutHandles();
    auto& context = tools.get_context();
    auto get_handles = [&] {
      auto overlay = CanvasOverlay();
      handles.draw(context, overlay);
      auto shapes = std::vector<OverlayShape>();
      for(auto& shape : overlay.m_shapes) {
        if(shape.m_radius == LayoutHandles::HANDLE_RADIUS) {
          shapes.push_back(shape);
        }
      }
      return shapes;
    };
    auto shapes = get_handles();
    REQUIRE(shapes.size() == 2);
    REQUIRE(shapes[0].m_center.m_x == doctest::Approx(199.5));
    REQUIRE(shapes[0].m_size.m_width == doctest::Approx(9));
    REQUIRE(shapes[0].m_size.m_height == doctest::Approx(40.0 / 3 + 1));
    REQUIRE(shapes[1].m_center.m_y == doctest::Approx(139.5));
    REQUIRE(shapes[1].m_size.m_width == doctest::Approx(100.0 / 3 + 1));
    REQUIRE(shapes[0].m_fill == make_color(255, 255, 255));
    REQUIRE(shapes[0].m_stroke == make_color(30, 203, 64));
    REQUIRE(handles.hover(context, Point(203.5, 127)));
    REQUIRE(handles.hover(context, Point(195.5, 113)));
    REQUIRE(!handles.hover(context, Point(204.5, 120)));
    REQUIRE(!handles.hover(context, Point(194.5, 120)));
    REQUIRE(!handles.hover(context, Point(200, 127.5)));
    REQUIRE(handles.hover(context, Point(200, 120)));
    REQUIRE(get_handles()[0].m_fill == make_color(30, 203, 64));
    tools.set_view(make_scale(2, 2), 2, Size(2000, 1600));
    shapes = get_handles();
    REQUIRE(shapes[0].m_size.m_width == doctest::Approx(9));
    REQUIRE(shapes[0].m_size.m_height == doctest::Approx(80.0 / 3 + 1));
    REQUIRE(shapes[1].m_size.m_width == doctest::Approx(39));
  }

  TEST_CASE("grid_gaps") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_repeat_grid =
      RepeatGrid(Size(40, 40), 2, 20, 3, 1, Size(124, 40));
    auto grid = transaction.append(
      NodeId(), make_header("Grid", make_translation(100, 100)), group);
    for(auto i = 0; i != 3; ++i) {
      auto cell = transaction.append(
        grid, make_header("", make_translation(i * 42.0, 0)), Group());
      transaction.append(
        cell, make_header("Rectangle"), make_rectangle_shape(40, 40));
    }
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(grid);
    auto handles = LayoutHandles();
    auto& context = tools.get_context();
    auto draw = [&] {
      auto overlay = CanvasOverlay();
      handles.draw(context, overlay);
      auto fills = std::vector<Point>();
      auto sides = std::vector<Point>();
      for(auto& shape : overlay.m_shapes) {
        if(shape.m_fill == make_color(255, 74, 225, 0.4)) {
          fills.push_back(shape.m_center);
        } else if(shape.m_fill == make_color(255, 74, 225)) {
          sides.push_back(shape.m_center);
        }
      }
      return std::pair(fills, sides);
    };
    REQUIRE(draw().first.empty());
    REQUIRE(!handles.hover(context, Point(138.5, 120)));
    REQUIRE(handles.hover(context, Point(143, 120)));
    REQUIRE(handles.hover(context, Point(141.5, 120)));
    auto [fills, sides] = draw();
    REQUIRE(fills == std::vector{Point(141, 120)});
    REQUIRE(sides == std::vector{Point(139.5, 120), Point(142.5, 120)});
    REQUIRE(handles.press(context, PointerEvent(Point(141, 120),
      Qt::LeftButton, Qt::LeftButton, Qt::KeyboardModifiers())));
    std::tie(fills, sides) = draw();
    REQUIRE(fills == std::vector{Point(141, 120), Point(183, 120)});
    REQUIRE(sides.size() == 2);
    auto overlay = CanvasOverlay();
    handles.draw(context, overlay);
    REQUIRE(overlay.m_images.size() == 2);
    REQUIRE(overlay.m_images[0].m_position.m_y < 100);
    handles.cancel(context);
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
      make_header("Last", make_translation(0, 80)),
      make_rectangle_shape(100, 20));
    auto middle = transaction.append(stack,
      make_header("Middle", make_translation(0, 50)),
      make_rectangle_shape(100, 20));
    auto first = transaction.append(
      stack, make_header("First"), make_rectangle_shape(100, 40));
    auto payload = *document.get(stack).get_group();
    payload.m_stack->m_spacing.emplace(first, 10);
    payload.m_stack->m_spacing.emplace(middle, 10);
    transaction.replace_payload(stack, payload);
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& context = tools.get_context();
    auto get_spacing = [&] (const NodeId& item) {
      return document.get(stack).get_group()->m_stack->m_spacing.at(item);
    };
    editor.get_selection().select(stack);
    auto handles = LayoutHandles();
    REQUIRE(handles.get_stacks(context).empty());
    REQUIRE(!handles.hover(context, Point(150, 145)));
    REQUIRE(handles.key_press(KeyEvent(Qt::Key_S, Qt::NoModifier)));
    REQUIRE(!handles.key_press(KeyEvent(Qt::Key_S, Qt::NoModifier, true)));
    REQUIRE(handles.get_stacks(context) == std::vector{stack});
    auto cursor = handles.hover(context, Point(150, 145));
    REQUIRE(cursor);
    REQUIRE(cursor->m_kind == CursorKind::RESIZE);
    REQUIRE(!handles.hover(context, Point(150, 120)));
    REQUIRE(handles.hover(context, Point(150, 145)));
    auto overlay = CanvasOverlay();
    handles.draw(context, overlay);
    auto fills = std::size_t(0);
    auto sides = std::size_t(0);
    for(auto& shape : overlay.m_shapes) {
      if(shape.m_fill == make_color(255, 74, 225, 0.4)) {
        ++fills;
      } else if(shape.m_fill == make_color(255, 74, 225)) {
        ++sides;
      }
    }
    REQUIRE(fills == 1);
    REQUIRE(sides == 2);
    REQUIRE(overlay.m_images.size() == 1);
    REQUIRE(!handles.key_release(KeyEvent(Qt::Key_S, Qt::NoModifier, true)));
    REQUIRE(handles.key_release(KeyEvent(Qt::Key_S, Qt::NoModifier)));
    REQUIRE(!handles.hover(context, Point(150, 145)));
    drag(tools, Point(150, 145), Point(160, 146));
    REQUIRE(get_spacing(first) == 10);
    REQUIRE(get_transform(document, stack) == make_translation(100, 100));
    REQUIRE(editor.get_selection().is_empty());
    REQUIRE(editor.get_history().get_undo_count() == 0);
    editor.get_selection().select(stack);
    tools.key_press(KeyEvent(Qt::Key_QuoteLeft, Qt::NoModifier));
    drag(tools, Point(150, 145), Point(150, 155));
    REQUIRE(get_transform(document, middle) == make_translation(0, 60));
    REQUIRE(get_spacing(first) == 20);
    REQUIRE(get_spacing(middle) == 10);
    REQUIRE(editor.get_history().get_undo_label() == "Stack Spacing");
    REQUIRE(editor.get_history().get_undo_count() == 1);
    tools.press(PointerEvent(Point(150, 185), Qt::LeftButton, Qt::LeftButton,
      Qt::ShiftModifier));
    tools.move(PointerEvent(Point(150, 190), Qt::NoButton, Qt::LeftButton,
      Qt::ShiftModifier));
    REQUIRE(get_spacing(first) == 15);
    REQUIRE(get_spacing(middle) == 15);
    tools.move(PointerEvent(Point(150, 190), Qt::NoButton, Qt::LeftButton,
      Qt::NoModifier));
    REQUIRE(get_spacing(first) == 20);
    REQUIRE(get_spacing(middle) == 15);
    tools.release(PointerEvent(Point(150, 190), Qt::LeftButton, Qt::NoButton,
      Qt::NoModifier));
    REQUIRE(editor.get_history().get_undo_count() == 2);
    REQUIRE(editor.get_history().get_undo_label() == "Stack Spacing");
    tools.key_release(KeyEvent(Qt::Key_QuoteLeft, Qt::NoModifier));
    REQUIRE(editor.get_context().enter(stack));
    editor.get_selection().select(first);
    REQUIRE(handles.get_stacks(context) == std::vector{stack});
    REQUIRE(handles.hover(context, Point(150, 145)));
    REQUIRE(!handles.hover(context, Point(150, 145), {}, true));
    REQUIRE(handles.hover(context, Point(150, 145)));
    overlay = CanvasOverlay();
    handles.draw(context, overlay);
    fills = 0;
    sides = 0;
    for(auto& shape : overlay.m_shapes) {
      if(shape.m_fill == make_color(255, 74, 225, 0.2)) {
        ++fills;
      } else if(shape.m_fill == make_color(255, 74, 225)) {
        ++sides;
      }
    }
    REQUIRE(fills == 1);
    REQUIRE(sides == 0);
    REQUIRE(overlay.m_images.size() == 1);
    tools.press(PointerEvent(Point(150, 145), Qt::LeftButton, Qt::LeftButton,
      Qt::NoModifier));
    tools.move(PointerEvent(Point(150, 150), Qt::NoButton, Qt::LeftButton,
      Qt::NoModifier));
    REQUIRE(get_spacing(first) == 25);
    REQUIRE(editor.get_selection().get_nodes() == std::vector{first});
    tools.release(PointerEvent(Point(150, 150), Qt::LeftButton, Qt::NoButton,
      Qt::NoModifier));
    REQUIRE(editor.get_selection().get_nodes() == std::vector{first});
    REQUIRE(editor.get_context().get_container() == stack);
    REQUIRE(editor.get_history().get_undo_label() == "Stack Spacing");
  }
}
