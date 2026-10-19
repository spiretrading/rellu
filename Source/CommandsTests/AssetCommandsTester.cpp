#include <doctest/doctest.h>
#include "Rellu/Commands/AssetCommands.hpp"
#include "Rellu/Commands/StyleCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  SwatchId add_swatch(History& history, const Color& color) {
    auto command = std::make_unique<AddSwatchCommand>(
      ColorSwatch(SwatchId(), std::string(), color));
    auto id = command->get_id();
    REQUIRE(history.execute(std::move(command)));
    return id;
  }
}

TEST_SUITE("AssetCommands") {
  TEST_CASE("add_swatch") {
    auto document = Document();
    build_scene(document);
    auto history = History(document);
    auto command = std::make_unique<AddSwatchCommand>(
      ColorSwatch(SwatchId(), "Brand", make_color(1, 2, 3)));
    auto id = command->get_id();
    require_undoable(history, std::move(command));
    REQUIRE(history.get_undo_label() == "Add Color");
    auto& swatches = document.get_resources().m_swatches;
    REQUIRE(swatches.size() == 1);
    REQUIRE(swatches.front().m_id == id);
    REQUIRE(swatches.front().m_name == "Brand");
  }

  TEST_CASE("replace_swatch") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto id = add_swatch(history, make_color(255, 0, 0));
    REQUIRE(history.execute(std::make_unique<SetFillCommand>(
      std::vector{scene.m_rectangle, scene.m_text},
      SolidFill(make_color(255, 0, 0), id))));
    auto swatch = document.get_resources().m_swatches.front();
    swatch.m_color = make_color(0, 0, 255);
    require_undoable(
      history, std::make_unique<ReplaceSwatchCommand>(swatch));
    REQUIRE(history.get_undo_label() == "Edit Color");
    REQUIRE(document.get(scene.m_rectangle).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 0, 255), id)));
    REQUIRE(document.get(scene.m_text).get_text()->m_runs.front().m_style.
      m_color == make_color(0, 0, 255));
    REQUIRE(document.get(scene.m_loose).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(255, 0, 0))));
    require_undoable(
      history, std::make_unique<RenameSwatchCommand>(id, "Blue"));
    REQUIRE(history.get_undo_label() == "Rename");
    REQUIRE(document.get_resources().m_swatches.front().m_name == "Blue");
  }

  TEST_CASE("remove_swatch") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto id = add_swatch(history, make_color(255, 0, 0));
    REQUIRE(history.execute(std::make_unique<SetFillCommand>(
      std::vector{scene.m_rectangle}, SolidFill(make_color(255, 0, 0), id))));
    require_undoable(history, std::make_unique<RemoveSwatchCommand>(id));
    REQUIRE(history.get_undo_label() == "Delete");
    REQUIRE(document.get_resources().m_swatches.empty());
    REQUIRE(document.get(scene.m_rectangle).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(255, 0, 0))));
  }

  TEST_CASE("add_colors") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    REQUIRE(!make_add_colors_command(document, std::vector{scene.m_mask}));
    require_undoable(history, make_add_colors_command(
      document, std::vector{scene.m_rectangle, scene.m_loose}));
    REQUIRE(history.get_undo_label() == "Add Color");
    auto& swatches = document.get_resources().m_swatches;
    REQUIRE(swatches.size() == 2);
    REQUIRE(swatches[0].m_color == make_color(255, 0, 0));
    REQUIRE(swatches[1].m_color == make_color(0, 0, 0));
    REQUIRE(!make_add_colors_command(document, std::vector{scene.m_loose}));
    auto registry = CommandRegistry();
    add_asset_commands(registry);
    REQUIRE(registry.contains("object.add_color"));
    REQUIRE(registry.contains("object.add_character_style"));
  }

  TEST_CASE("find_swatch") {
    auto document = Document();
    build_scene(document);
    auto history = History(document);
    auto red = add_swatch(history, make_color(255, 0, 0));
    auto other = add_swatch(history, make_color(255, 0, 0));
    auto& resources = document.get_resources();
    REQUIRE(find_swatch(resources, SolidFill(make_color(255, 0, 0)))->m_id ==
      red);
    REQUIRE(find_swatch(resources,
      SolidFill(make_color(255, 0, 0), other))->m_id == other);
    REQUIRE(!find_swatch(resources, SolidFill(make_color(1, 0, 0))));
    REQUIRE(!find_swatch(resources, NoFill()));
  }

  TEST_CASE("character_styles") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto& text = *document.get(scene.m_text).get_text();
    REQUIRE(!find_character_style(document.get_resources(), text));
    require_undoable(history, make_add_character_styles_command(
      document, std::vector{scene.m_group}));
    REQUIRE(history.get_undo_label() == "Add Character Style");
    auto& styles = document.get_resources().m_character_styles;
    REQUIRE(styles.size() == 1);
    REQUIRE(styles.front().m_style.m_font.m_family == "Roboto");
    REQUIRE(find_character_style(document.get_resources(),
      *document.get(scene.m_text).get_text())->m_id == styles.front().m_id);
    REQUIRE(!make_add_character_styles_command(
      document, std::vector{scene.m_text}));
  }

  TEST_CASE("apply_character_style") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto style = CharacterStyle();
    style.m_name = "Heading";
    style.m_style.m_font = Font("Inter", "Bold", "Inter-Bold", 24);
    style.m_style.m_color = make_color(0, 0, 255);
    style.m_line_height = 30;
    auto add = std::make_unique<AddCharacterStyleCommand>(style);
    auto id = add->get_id();
    REQUIRE(history.execute(std::move(add)));
    require_undoable(history, std::make_unique<ApplyCharacterStyleCommand>(
      std::vector{scene.m_text, scene.m_rectangle}, id));
    REQUIRE(history.get_undo_label() == "Apply Character Style");
    auto& text = *document.get(scene.m_text).get_text();
    REQUIRE(text.m_runs.front().m_style.m_font.m_family == "Inter");
    REQUIRE(text.m_runs.front().m_style.m_font.m_size == 24);
    REQUIRE(text.m_runs.front().m_character_style == id);
    REQUIRE(text.m_paragraph_style.m_line_height == 30);
    auto edited = document.get_resources().m_character_styles.front();
    edited.m_style.m_font.m_size = 32;
    require_undoable(
      history, std::make_unique<ReplaceCharacterStyleCommand>(edited));
    REQUIRE(history.get_undo_label() == "Edit Character Style");
    REQUIRE(document.get(scene.m_text).get_text()->m_runs.front().m_style.
      m_font.m_size == 32);
    require_undoable(history,
      std::make_unique<RenameCharacterStyleCommand>(id, "Title"));
    REQUIRE(document.get_resources().m_character_styles.front().m_name ==
      "Title");
    require_undoable(
      history, std::make_unique<RemoveCharacterStyleCommand>(id));
    auto& removed = *document.get(scene.m_text).get_text();
    REQUIRE(removed.m_runs.front().m_character_style.is_nil());
    REQUIRE(removed.m_runs.front().m_style.m_font.m_size == 32);
  }

  TEST_CASE("swatch_fill") {
    auto solid = ColorSwatch(generate_node_id(), "", make_color(1, 2, 3));
    REQUIRE(make_swatch_fill(solid) ==
      Fill(SolidFill(make_color(1, 2, 3), solid.m_id)));
    auto gradient = solid;
    gradient.m_gradient = {GradientStop(0, make_color(0, 0, 0)),
      GradientStop(1, make_color(255, 255, 255))};
    auto fill = std::get<GradientFill>(make_swatch_fill(gradient));
    REQUIRE(fill.m_stops == gradient.m_gradient);
  }
}
