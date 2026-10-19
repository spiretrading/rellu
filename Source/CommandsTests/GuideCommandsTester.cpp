#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/GuideCommands.hpp"
#include "Rellu/Commands/History.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Board {
    NodeId m_first;
    NodeId m_second;
    NodeId m_child;
    NodeId m_other;
    NodeId m_loose;
  };

  Board build_board(Document& document) {
    auto board = Board();
    auto transaction = Transaction(document);
    auto first = Artboard();
    first.m_frame = Rect(0, 0, 100, 80);
    first.m_guides.push_back(Guide(Orientation::HORIZONTAL, 20, "h"));
    first.m_guides.push_back(Guide(Orientation::VERTICAL, 10, "v"));
    board.m_first = transaction.append(NodeId(), make_header("First"), first);
    auto second = Artboard();
    second.m_frame = Rect(200, 0, 50, 50);
    board.m_second =
      transaction.append(NodeId(), make_header("Second"), second);
    board.m_child = transaction.append(
      board.m_first, make_header("Child"), make_rectangle_shape(5, 5));
    board.m_other = transaction.append(
      board.m_second, make_header("Other"), make_rectangle_shape(5, 5));
    board.m_loose = transaction.append(
      NodeId(), make_header("Loose"), make_rectangle_shape(5, 5));
    transaction.commit();
    document.clear_history();
    return board;
  }

  const std::vector<Guide>& get_guides(
      const Document& document, const NodeId& id) {
    return document.get(id).get_artboard()->m_guides;
  }
}

TEST_SUITE("GuideCommands") {
  TEST_CASE("artboards") {
    auto document = Document();
    auto board = build_board(document);
    REQUIRE(get_guide_artboards(document, {board.m_child, board.m_second}) ==
      std::vector{board.m_second});
    REQUIRE(get_guide_artboards(document,
      {board.m_child, board.m_other, board.m_loose}) ==
      std::vector{board.m_first, board.m_second});
    REQUIRE(is_within_artboards(document, {board.m_child}, false, true));
    REQUIRE(!is_within_artboards(
      document, {board.m_child, board.m_other}, false, false));
    REQUIRE(is_within_artboards(
      document, {board.m_child, board.m_other}, true, false));
    REQUIRE(!is_within_artboards(
      document, {board.m_child, board.m_other}, true, true));
    REQUIRE(!is_within_artboards(document, {board.m_loose}, true, false));
    REQUIRE(!is_within_artboards(document, {board.m_first}, true, false));
  }

  TEST_CASE("insert") {
    auto guides = std::vector<Guide>();
    insert_guide(guides, Guide(Orientation::VERTICAL, 30, "a"));
    insert_guide(guides, Guide(Orientation::HORIZONTAL, 40, "b"));
    insert_guide(guides, Guide(Orientation::VERTICAL, 10, "c"));
    insert_guide(guides, Guide(Orientation::HORIZONTAL, 5, "d"));
    REQUIRE(guides == std::vector{Guide(Orientation::HORIZONTAL, 5, "d"),
      Guide(Orientation::HORIZONTAL, 40, "b"),
      Guide(Orientation::VERTICAL, 10, "c"),
      Guide(Orientation::VERTICAL, 30, "a")});
  }

  TEST_CASE("merge") {
    auto guides = std::vector{Guide(Orientation::VERTICAL, 10, "a")};
    auto merged = merge_guides(guides, {Guide(Orientation::VERTICAL, 10, "b"),
      Guide(Orientation::VERTICAL, 49, "c"),
      Guide(Orientation::VERTICAL, 50, "d"),
      Guide(Orientation::HORIZONTAL, 10, "e")}, Size(50, 30));
    REQUIRE(merged == std::vector{Guide(Orientation::HORIZONTAL, 10, "e"),
      Guide(Orientation::VERTICAL, 10, "a"),
      Guide(Orientation::VERTICAL, 49, "c")});
  }

  TEST_CASE("set") {
    auto document = Document();
    auto board = build_board(document);
    auto history = History(document);
    auto guides = get_guides(document, board.m_first);
    guides.pop_back();
    require_undoable(history, std::make_unique<SetGuidesCommand>(
      "Delete Guide", board.m_first, guides));
    REQUIRE(get_guides(document, board.m_first) == guides);
  }

  TEST_CASE("clear") {
    auto document = Document();
    auto board = build_board(document);
    auto history = History(document);
    require_undoable(history, std::make_unique<ClearGuidesCommand>(
      std::vector{board.m_first, board.m_second}));
    REQUIRE(get_guides(document, board.m_first).empty());
  }

  TEST_CASE("paste") {
    auto document = Document();
    auto board = build_board(document);
    auto history = History(document);
    auto count = 0;
    require_undoable(history, std::make_unique<PasteGuidesCommand>(
      std::vector{board.m_first, board.m_second},
      get_guides(document, board.m_first), [&] {
        return std::to_string(++count);
      }));
    REQUIRE(get_guides(document, board.m_first).size() == 2);
    REQUIRE(get_guides(document, board.m_second) == std::vector{
      Guide(Orientation::HORIZONTAL, 20, "3"),
      Guide(Orientation::VERTICAL, 10, "4")});
  }
}
