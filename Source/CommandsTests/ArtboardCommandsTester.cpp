#include <doctest/doctest.h>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("ArtboardCommands") {
  TEST_CASE("default_grid") {
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 480, 360);
    auto grid = make_default_grid(artboard);
    REQUIRE(grid.m_type == GridType::LAYOUT);
    REQUIRE(!grid.m_is_visible);
    REQUIRE(grid.m_column_count == 4);
    REQUIRE(get_column_width(grid, 480) == 94);
    REQUIRE(grid.m_square_size == Size(8, 8));
  }

  TEST_CASE("grid") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto edit = GridEdit();
    edit.m_is_visible = true;
    require_undoable(history, std::make_unique<SetGridCommand>(
      std::vector{scene.m_artboard, scene.m_rectangle}, edit));
    REQUIRE(history.get_undo_label() == "Change Grid");
    auto& grid = *document.get(scene.m_artboard).get_artboard()->m_layout_grid;
    REQUIRE(grid.m_is_visible);
    REQUIRE(grid.m_column_count == 4);
    auto columns = GridEdit();
    columns.m_column_count = 5;
    columns.m_column_width = 60;
    require_undoable(history, std::make_unique<SetGridCommand>(
      std::vector{scene.m_artboard}, columns));
    auto& resized =
      *document.get(scene.m_artboard).get_artboard()->m_layout_grid;
    REQUIRE(resized.m_column_count == 5);
    REQUIRE(resized.m_margins.m_left == 30);
    REQUIRE(resized.m_margins.m_right == 30);
    REQUIRE(get_column_width(resized, 400) == doctest::Approx(60));
  }

  TEST_CASE("viewport") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history, std::make_unique<SetViewportCommand>(
      std::vector{scene.m_artboard}, std::nullopt, true));
    REQUIRE(history.get_undo_label() == "Change Scrolling");
    REQUIRE(document.get(scene.m_artboard).get_artboard()->m_viewport_height ==
      300);
    require_undoable(history, std::make_unique<SetViewportCommand>(
      std::vector{scene.m_artboard}, 200, true));
    REQUIRE(document.get(scene.m_artboard).get_artboard()->m_viewport_height ==
      200);
    require_undoable(history, std::make_unique<SetViewportCommand>(
      std::vector{scene.m_artboard}, std::nullopt, false));
    REQUIRE(
      !document.get(scene.m_artboard).get_artboard()->m_viewport_height);
  }

  TEST_CASE("grid_toggles") {
    auto document = Document();
    auto scene = build_scene(document);
    REQUIRE(get_target_artboards(document, std::vector{scene.m_ellipse}) ==
      std::vector{scene.m_artboard});
    REQUIRE(get_target_artboards(document, std::vector<NodeId>()) ==
      std::vector{scene.m_artboard});
    auto registry = CommandRegistry();
    add_artboard_commands(registry);
    auto history = History(document);
    require_undoable(history, registry.make(
      "view.square_grid", document, std::vector<NodeId>()));
    auto& grid = *document.get(scene.m_artboard).get_artboard()->m_layout_grid;
    REQUIRE(grid.m_type == GridType::SQUARE);
    REQUIRE(grid.m_is_visible);
    require_undoable(history, registry.make(
      "view.square_grid", document, std::vector<NodeId>()));
    REQUIRE(!document.get(scene.m_artboard).get_artboard()->m_layout_grid->
      m_is_visible);
  }
}
