#include <doctest/doctest.h>
#include "Rellu/Commands/StyleCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("StyleCommands") {
  TEST_CASE("fill") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto fill = Fill(SolidFill(make_color(0, 128, 0)));
    require_undoable(history, std::make_unique<SetFillCommand>(
      std::vector{scene.m_rectangle, scene.m_group, scene.m_artboard},
      fill));
    REQUIRE(history.get_undo_label() == "Change Fill");
    REQUIRE(document.get(scene.m_rectangle).get_shape()->m_style.m_fill ==
      fill);
    REQUIRE(document.get(scene.m_ellipse).get_shape()->m_style.m_fill ==
      fill);
    auto& text = *document.get(scene.m_text).get_text();
    REQUIRE(text.m_style.m_fill == fill);
    REQUIRE(text.m_runs.front().m_style.m_color == make_color(0, 128, 0));
    REQUIRE(document.get(scene.m_artboard).get_artboard()->m_background ==
      fill);
    REQUIRE(document.get(scene.m_background).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(255, 0, 0))));
  }

  TEST_CASE("fill_merge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle};
    REQUIRE(history.merge(std::make_unique<SetFillCommand>(
      nodes, SolidFill(make_color(1, 2, 3)))));
    REQUIRE(history.merge(std::make_unique<SetFillCommand>(
      nodes, SolidFill(make_color(4, 5, 6)))));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(document.get(scene.m_rectangle).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(4, 5, 6))));
    REQUIRE(history.merge(std::make_unique<SetFillCommand>(
      nodes, SolidFill(make_color(7, 8, 9)), "Nudge Left")));
    REQUIRE(history.merge(std::make_unique<SetFillCommand>(
      nodes, SolidFill(make_color(10, 11, 12)), "Nudge Left")));
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(history.get_undo_label() == "Nudge Left");
    REQUIRE(history.merge(std::make_unique<SetFillCommand>(
      nodes, SolidFill(make_color(13, 14, 15)))));
    REQUIRE(history.get_undo_count() == 3);
    history.undo();
    REQUIRE(document.get(scene.m_rectangle).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(10, 11, 12))));
  }

  TEST_CASE("stroke") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto edit = StrokeEdit();
    edit.m_width = 3;
    edit.m_alignment = StrokeAlignment::INSIDE;
    require_undoable(history, std::make_unique<SetStrokeCommand>(
      std::vector{scene.m_rectangle, scene.m_ellipse}, edit));
    REQUIRE(history.get_undo_label() == "Change Stroke");
    auto& rectangle =
      *document.get(scene.m_rectangle).get_shape()->m_style.m_stroke;
    REQUIRE(rectangle.m_width == 3);
    REQUIRE(rectangle.m_alignment == StrokeAlignment::INSIDE);
    REQUIRE(rectangle.m_color == make_color(0, 0, 0));
    auto& ellipse =
      *document.get(scene.m_ellipse).get_shape()->m_style.m_stroke;
    REQUIRE(ellipse.m_width == 3);
    REQUIRE(ellipse.m_cap == StrokeCap::BUTT);
  }

  TEST_CASE("stroke_merge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle};
    auto width = StrokeEdit();
    width.m_width = 8;
    auto color = StrokeEdit();
    color.m_color = make_color(0, 0, 255);
    REQUIRE(history.merge(std::make_unique<SetStrokeCommand>(nodes, width)));
    REQUIRE(history.merge(std::make_unique<SetStrokeCommand>(nodes, color)));
    REQUIRE(history.get_undo_count() == 1);
    auto& stroke =
      *document.get(scene.m_rectangle).get_shape()->m_style.m_stroke;
    REQUIRE(stroke.m_width == 8);
    REQUIRE(stroke.m_color == make_color(0, 0, 255));
  }
}
