#include <doctest/doctest.h>
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Commands/History.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Board {
    NodeId m_artboard;
    NodeId m_rectangle;
    NodeId m_text;
    NodeId m_group;
  };

  Board build_board(Document& document) {
    auto board = Board();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 50, 400, 300);
    board.m_artboard =
      transaction.append(NodeId(), make_header("Artboard"), artboard);
    board.m_rectangle = transaction.append(board.m_artboard,
      make_header("Rectangle", make_translation(10, 10)),
      make_rectangle_shape(40, 20));
    auto text = make_text(u"Label");
    text.m_frame = AreaFrame(Size(40, 20));
    board.m_text = transaction.append(board.m_artboard,
      make_header("Text", make_translation(10, 30)), text);
    board.m_group = transaction.append(board.m_artboard,
      make_header("Card", make_translation(200, 100)), Group());
    transaction.append(board.m_group, make_header("Face"),
      make_rectangle_shape(30, 30));
    transaction.commit();
    document.clear_history();
    return board;
  }

  const RepeatGrid& get_grid(const Document& document, const NodeId& id) {
    return *document.get(id).get_group()->m_repeat_grid;
  }

  Point get_offset(const Document& document, const NodeId& id) {
    auto transform = get_transform(document, id);
    return Point(transform.m_tx, transform.m_ty);
  }

  NodeId make_grid(History& history, const Board& board) {
    auto command = history.execute(std::make_unique<RepeatGridCommand>(
      std::vector{board.m_rectangle, board.m_text}));
    return static_cast<const RepeatGridCommand&>(*command).get_grid();
  }
}

TEST_SUITE("RepeatGridCommands") {
  TEST_CASE("make") {
    auto document = Document();
    auto board = build_board(document);
    auto history = History(document);
    auto grid = make_grid(history, board);
    REQUIRE(history.get_undo_label() == "Repeat Grid");
    auto& node = document.get(grid);
    REQUIRE(node.m_header.m_name == "Repeat Grid 1");
    REQUIRE(node.m_parent == board.m_artboard);
    REQUIRE(get_transform(document, grid) == make_translation(10, 10));
    REQUIRE(get_grid(document, grid) ==
      RepeatGrid(Size(40, 40), 20, 20, 1, 1, Size(40, 40)));
    REQUIRE(node.m_children.size() == 1);
    auto cell = node.m_children.front();
    REQUIRE(get_transform(document, cell) == make_translation(-10, -10));
    REQUIRE(document.get(cell).m_header.m_name.empty());
    REQUIRE(document.get(cell).m_header.m_is_naming_disabled);
    REQUIRE(document.get(cell).m_children ==
      std::vector{board.m_rectangle, board.m_text});
    REQUIRE(get_world_bounds(document, board.m_rectangle) ==
      Rect(110, 60, 40, 20));
    REQUIRE(is_repeat_grid(document, grid));
    history.undo();
    REQUIRE(!document.find(grid));
    REQUIRE(document.get(board.m_rectangle).m_parent == board.m_artboard);
    auto group = history.execute(std::make_unique<RepeatGridCommand>(
      std::vector{board.m_group}));
    auto card = static_cast<const RepeatGridCommand&>(*group).get_grid();
    REQUIRE(document.get(card).m_children == std::vector{board.m_group});
    REQUIRE(!document.get(board.m_group).m_header.m_is_naming_disabled);
    REQUIRE(get_world_bounds(document, board.m_group) ==
      Rect(300, 150, 30, 30));
  }

  TEST_CASE("resize") {
    auto document = Document();
    auto board = build_board(document);
    auto history = History(document);
    auto grid = make_grid(history, board);
    history.execute(
      std::make_unique<ResizeRepeatGridCommand>(grid, Size(40, 160)));
    REQUIRE(get_grid(document, grid).m_row_count == 3);
    REQUIRE(get_grid(document, grid).m_size == Size(40, 160));
    auto cells = document.get(grid).m_children;
    REQUIRE(cells.size() == 3);
    REQUIRE(get_offset(document, cells[1]) == Point(-10, 50));
    REQUIRE(get_offset(document, cells[2]) == Point(-10, 110));
    auto copy = document.get(cells[2]).m_children;
    REQUIRE(copy.size() == 2);
    REQUIRE(*document.get(copy[1]).get_text()->m_content == u"Label");
    history.merge(
      std::make_unique<ResizeRepeatGridCommand>(grid, Size(100, 160)));
    REQUIRE(history.get_undo_count() == 2);
    cells = document.get(grid).m_children;
    REQUIRE(cells.size() == 6);
    REQUIRE(get_offset(document, cells[1]) == Point(50, -10));
    REQUIRE(get_offset(document, cells[5]) == Point(50, 110));
    history.execute(
      std::make_unique<ResizeRepeatGridCommand>(grid, Size(100, 50)));
    REQUIRE(get_grid(document, grid).m_row_count == 1);
    REQUIRE(document.get(grid).m_children.size() == 2);
    history.undo();
    REQUIRE(document.get(grid).m_children == cells);
  }

  TEST_CASE("spacing") {
    auto document = Document();
    auto board = build_board(document);
    auto history = History(document);
    auto grid = make_grid(history, board);
    history.execute(
      std::make_unique<ResizeRepeatGridCommand>(grid, Size(40, 160)));
    history.execute(std::make_unique<SetRepeatGridSpacingCommand>(
      std::vector{grid}, Orientation::VERTICAL, 5));
    REQUIRE(history.get_undo_label() == "Spacing");
    REQUIRE(get_grid(document, grid).m_row_spacing == 5);
    REQUIRE(get_grid(document, grid).m_size == Size(40, 130));
    auto& cells = document.get(grid).m_children;
    REQUIRE(get_offset(document, cells[1]) == Point(-10, 35));
    REQUIRE(get_offset(document, cells[2]) == Point(-10, 80));
    history.merge(std::make_unique<SetRepeatGridSpacingCommand>(
      std::vector{grid}, Orientation::VERTICAL, -100));
    REQUIRE(get_grid(document, grid).m_row_spacing == -39);
    REQUIRE(history.get_undo_count() == 3);
  }

  TEST_CASE("ungroup") {
    auto document = Document();
    auto board = build_board(document);
    auto history = History(document);
    auto grid = make_grid(history, board);
    REQUIRE(!make_ungroup_grid_command(document,
      std::vector{grid, board.m_group}));
    auto command = make_ungroup_grid_command(document, std::vector{grid});
    REQUIRE(command->get_label() == "Ungroup Grid");
    history.execute(std::move(command));
    REQUIRE(!is_repeat_grid(document, grid));
    REQUIRE(document.get(grid).m_children.size() == 1);
    auto registry = CommandRegistry();
    add_standard_commands(registry);
    history.undo();
    REQUIRE(registry.make("object.ungroup", document, std::vector{grid})->
      get_label() == "Ungroup Grid");
  }

  TEST_CASE("registry") {
    auto document = Document();
    auto board = build_board(document);
    auto registry = CommandRegistry();
    add_repeat_grid_commands(registry);
    REQUIRE(!registry.make("object.repeat_grid", document, {}));
    REQUIRE(!registry.make(
      "object.repeat_grid", document, std::vector{board.m_artboard}));
    auto command = registry.make(
      "object.repeat_grid", document, std::vector{board.m_rectangle});
    REQUIRE(command->get_label() == "Repeat Grid");
    auto history = History(document);
    auto made = history.execute(std::move(command));
    auto grid = static_cast<const RepeatGridCommand&>(*made).get_grid();
    REQUIRE(!registry.make("object.repeat_grid", document, std::vector{grid}));
  }

  TEST_CASE("text_series") {
    auto document = Document();
    auto board = build_board(document);
    {
      auto transaction = Transaction(document);
      auto text = *document.get(board.m_text).get_text();
      text.m_repeat_data = {u"Label", u"Second", u"Third"};
      transaction.replace_payload(board.m_text, Box<Text>(std::move(text)));
      transaction.commit();
    }
    auto history = History(document);
    auto grid = make_grid(history, board);
    history.execute(
      std::make_unique<ResizeRepeatGridCommand>(grid, Size(40, 230)));
    auto& cells = document.get(grid).m_children;
    REQUIRE(cells.size() == 4);
    auto get_content = [&] (std::size_t index) {
      auto& cell = document.get(cells[index]);
      return *document.get(cell.m_children.back()).get_text()->m_content;
    };
    REQUIRE(get_content(0) == u"Label");
    REQUIRE(get_content(1) == u"Second");
    REQUIRE(get_content(2) == u"Third");
    REQUIRE(get_content(3) == u"Label");
    auto& copy = *document.get(
      document.get(cells[1]).m_children.back()).get_text();
    REQUIRE(copy.m_runs.size() == 1);
    REQUIRE(copy.m_runs.front().m_length == 6);
    REQUIRE(copy.m_repeat_data.size() == 3);
  }

  TEST_CASE("cell_text_edit") {
    auto document = Document();
    auto board = build_board(document);
    auto history = History(document);
    auto command = history.execute(
      std::make_unique<RepeatGridCommand>(std::vector{board.m_text}));
    auto grid = static_cast<const RepeatGridCommand&>(*command).get_grid();
    history.execute(
      std::make_unique<ResizeRepeatGridCommand>(grid, Size(140, 20)));
    auto cells = document.get(grid).m_children;
    REQUIRE(cells.size() == 3);
    auto get_text = [&] (std::size_t index) -> const Text& {
      return *document.get(
        document.get(cells[index]).m_children.back()).get_text();
    };
    auto edited = replace_text(get_text(1), TextRange(0, 5), u"hi");
    history.execute(std::make_unique<EditTextCommand>(
      document.get(cells[1]).m_children.back(), std::move(edited)));
    REQUIRE(*get_text(0).m_content == u"Label");
    REQUIRE(*get_text(1).m_content == u"hi");
    REQUIRE(*get_text(2).m_content == u"Label");
    for(auto i = std::size_t(0); i != cells.size(); ++i) {
      REQUIRE(get_text(i).m_repeat_data.empty());
      REQUIRE(document.get(cells[i]).m_header.m_name.empty());
    }
  }

  TEST_CASE("measurer") {
    auto document = Document();
    auto board = build_board(document);
    auto history = History(document);
    auto measurer = [] (const Text&) { return Rect(0, 0, 60, 25); };
    history.set_text_measurer(measurer);
    auto command = history.execute(std::make_unique<RepeatGridCommand>(
      std::vector{board.m_rectangle, board.m_text}, measurer));
    auto grid = static_cast<const RepeatGridCommand&>(*command).get_grid();
    REQUIRE(get_grid(document, grid).m_cell_size == Size(60, 45));
  }
}
