#include <doctest/doctest.h>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/Bounds.hpp"
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
    REQUIRE(grid.m_default_width == 480);
    REQUIRE(get_column_width(grid, 480) == 94);
    REQUIRE(grid.m_square_size == Size(8, 8));
    struct Expected {
      double m_width;
      int m_columns;
      double m_gutter;
      double m_column;
      double m_left;
      double m_right;
    };
    for(auto& expected : {Expected(50, 1, 0, 1, 24, 25),
        Expected(200, 1, 0, 144, 28, 28), Expected(312, 1, 0, 256, 28, 28),
        Expected(313, 4, 10, 53, 35, 35), Expected(375, 4, 10, 68, 36, 36),
        Expected(481, 4, 10, 95, 35, 35), Expected(600, 8, 16, 41, 80, 80),
        Expected(834, 8, 16, 70, 81, 81), Expected(1024, 12, 16, 51, 118, 118),
        Expected(1350, 12, 16, 74, 143, 143),
        Expected(1920, 12, 16, 122, 140, 140),
        Expected(2560, 12, 16, 175, 142, 142)}) {
      artboard.m_frame.m_width = expected.m_width;
      auto layout = make_default_grid(artboard);
      REQUIRE(layout.m_column_count == expected.m_columns);
      REQUIRE(layout.m_gutter == expected.m_gutter);
      REQUIRE(layout.m_margins == Margins(0, expected.m_right, 0,
        expected.m_left));
      REQUIRE(get_column_width(layout, expected.m_width) == expected.m_column);
    }
  }

  TEST_CASE("use_default") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto columns = GridEdit();
    columns.m_is_visible = true;
    columns.m_column_count = 7;
    columns.m_gutter = 3;
    columns.m_left = 5;
    require_undoable(history, std::make_unique<SetGridCommand>(
      std::vector{scene.m_artboard}, columns));
    auto reset = GridEdit();
    reset.m_is_default = true;
    require_undoable(history, std::make_unique<SetGridCommand>(
      std::vector{scene.m_artboard}, reset));
    auto& grid = *document.get(scene.m_artboard).get_artboard()->m_layout_grid;
    REQUIRE(grid.m_is_visible);
    REQUIRE(grid.m_column_count == 4);
    REQUIRE(grid.m_gutter == 10);
    REQUIRE(grid.m_margins == Margins(0, 37, 0, 37));
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

  TEST_CASE("make_default") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto columns = GridEdit();
    columns.m_column_count = 6;
    columns.m_gutter = 16;
    columns.m_left = 20;
    columns.m_right = 20;
    columns.m_square_size = 12;
    REQUIRE(history.execute(std::make_unique<SetGridCommand>(
      std::vector{scene.m_artboard}, columns)));
    REQUIRE(!history.execute(
      std::make_unique<MakeGridDefaultCommand>(scene.m_artboard)));
    auto& defaults = document.get_resources().m_grid_defaults;
    REQUIRE(defaults.m_grid);
    REQUIRE(defaults.m_grid->m_column_count == 6);
    REQUIRE(defaults.m_grid->m_default_width == 400);
    REQUIRE(defaults.m_overrides);
    REQUIRE(defaults.m_overrides->size() == 5);
    REQUIRE(defaults.m_overrides->back().m_maximum_width == 400);
    REQUIRE(defaults.m_overrides->front() == get_built_in_overrides().front());
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 380, 300);
    auto grid = make_default_grid(artboard, defaults);
    REQUIRE(grid.m_column_count == 6);
    REQUIRE(grid.m_gutter == 16);
    REQUIRE(grid.m_square_size == Size(12, 12));
    REQUIRE(grid.m_margins == Margins(0, 21, 0, 21));
    artboard.m_frame.m_width = 700;
    REQUIRE(make_default_grid(artboard, defaults).m_column_count == 8);
    artboard.m_frame.m_width = 2000;
    REQUIRE(make_default_grid(artboard, defaults).m_column_count == 12);
    auto reset = GridEdit();
    reset.m_is_default = true;
    REQUIRE(history.execute(std::make_unique<SetGridCommand>(
      std::vector{scene.m_artboard}, reset)));
    REQUIRE(document.get(scene.m_artboard).get_artboard()->m_layout_grid->
      m_margins == Margins(0, 19, 0, 19));
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

  TEST_CASE("orientation") {
    auto document = Document();
    auto scene = build_scene(document);
    auto edge = NodeId();
    {
      auto transaction = Transaction(document);
      auto artboard = *document.get(scene.m_artboard).get_artboard();
      artboard.m_guides.push_back(Guide(Orientation::VERTICAL, 350));
      artboard.m_guides.push_back(Guide(Orientation::HORIZONTAL, 250));
      transaction.replace_payload(scene.m_artboard, artboard);
      edge = transaction.append(scene.m_artboard,
        make_header("Edge", make_translation(350, 10)),
        make_rectangle_shape(20, 20));
      transaction.commit();
    }
    auto& artboard = *document.get(scene.m_artboard).get_artboard();
    REQUIRE(get_orientation(artboard) == ArtboardOrientation::LANDSCAPE);
    auto history = History(document);
    require_undoable(history, std::make_unique<SetOrientationCommand>(
      std::vector{scene.m_artboard, scene.m_rectangle},
      ArtboardOrientation::PORTRAIT));
    REQUIRE(history.get_undo_label() == "Resize");
    auto& portrait = *document.get(scene.m_artboard).get_artboard();
    REQUIRE(portrait.m_frame == Rect(100, 50, 300, 400));
    REQUIRE(portrait.m_viewport_height == 400.0);
    REQUIRE(portrait.m_guides ==
      std::vector{Guide(Orientation::HORIZONTAL, 250)});
    REQUIRE(get_orientation(portrait) == ArtboardOrientation::PORTRAIT);
    REQUIRE(document.get(edge).m_parent.is_nil());
    REQUIRE(document.get(scene.m_rectangle).m_parent == scene.m_artboard);
    REQUIRE(get_transform(document, scene.m_rectangle) ==
      make_translation(10, 10));
    {
      auto transaction = Transaction(document);
      auto scrolling = *document.get(scene.m_artboard).get_artboard();
      scrolling.m_frame = Rect(100, 50, 300, 2000);
      scrolling.m_viewport_height = 200;
      transaction.replace_payload(scene.m_artboard, scrolling);
      transaction.commit();
    }
    REQUIRE(history.execute(std::make_unique<SetOrientationCommand>(
      std::vector{scene.m_artboard}, ArtboardOrientation::PORTRAIT)));
    REQUIRE(document.get(scene.m_artboard).get_artboard()->m_frame ==
      Rect(100, 50, 200, 300));
    REQUIRE(document.get(scene.m_artboard).get_artboard()->
      m_viewport_height == 300.0);
    {
      auto transaction = Transaction(document);
      auto square = *document.get(scene.m_artboard).get_artboard();
      square.m_frame = Rect(100, 50, 300, 300);
      transaction.replace_payload(scene.m_artboard, square);
      transaction.commit();
    }
    history.execute(std::make_unique<SetOrientationCommand>(
      std::vector{scene.m_artboard}, ArtboardOrientation::PORTRAIT));
    REQUIRE(document.get(scene.m_artboard).get_artboard()->m_frame ==
      Rect(100, 50, 300, 300));
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
