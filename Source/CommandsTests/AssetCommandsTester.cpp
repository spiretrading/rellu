#include <stdexcept>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/AssetCommands.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
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
    REQUIRE(history.get_undo_label() == "Add Color to Assets");
    auto& swatches = document.get_resources().m_swatches;
    REQUIRE(swatches.size() == 1);
    REQUIRE(swatches.front().m_id == id);
    REQUIRE(swatches.front().m_name == "Brand");
    REQUIRE(swatches.front().m_placements ==
      std::vector{AssetPlacement(COLOR_SECTION, "h")});
    auto next = add_swatch(history, make_color(4, 5, 6));
    REQUIRE(find_swatch(document.get_resources(),
      SolidFill(make_color(4, 5, 6)))->m_placements ==
        std::vector{AssetPlacement(COLOR_SECTION, "e")});
    REQUIRE(list_assets(document.get_resources(), COLOR_SECTION) ==
      std::vector{AssetEntry(std::string(), next),
        AssetEntry(std::string(), id)});
  }

  TEST_CASE("orders") {
    REQUIRE(make_order_between("", "") == "n");
    REQUIRE(make_order_between("", "n") == "h");
    REQUIRE(make_order_between("a", "b") == "an");
    REQUIRE(make_order_between("h", "") == "r");
    REQUIRE(make_orders_between(3, "", "n") ==
      std::vector<std::string>({"e", "h", "k"}));
    REQUIRE(make_orders_between(0, "", "").empty());
  }

  TEST_CASE("asset_groups") {
    auto document = Document();
    build_scene(document);
    auto history = History(document);
    auto red = add_swatch(history, make_color(255, 0, 0));
    auto green = add_swatch(history, make_color(0, 255, 0));
    auto blue = add_swatch(history, make_color(0, 0, 255));
    auto& resources = document.get_resources();
    REQUIRE(find_section(resources, red) == COLOR_SECTION);
    REQUIRE(find_section(resources, NodeId()).empty());
    auto group = std::make_unique<GroupAssetsCommand>(
      std::vector{green, red});
    auto id = group->get_group();
    require_undoable(history, std::move(group));
    REQUIRE(history.get_undo_label() == "Create Asset Group");
    REQUIRE(resources.m_asset_groups.size() == 1);
    auto& created = resources.m_asset_groups.front();
    REQUIRE(created.m_name == "New group");
    REQUIRE(created.m_parent == COLOR_SECTION);
    REQUIRE(created.m_elements ==
      std::vector{to_string(green), to_string(red)});
    REQUIRE(find_asset_group(resources, red) == id);
    REQUIRE(list_assets(resources, COLOR_SECTION) ==
      std::vector{AssetEntry(std::string(), blue), AssetEntry(id, NodeId())});
    REQUIRE(list_assets(resources, id) ==
      std::vector{AssetEntry(std::string(), green),
        AssetEntry(std::string(), red)});
    require_undoable(history,
      std::make_unique<RenameAssetGroupCommand>(id, "Brand"));
    REQUIRE(history.get_undo_label() == "Rename Asset Group");
    REQUIRE(resources.m_asset_groups.front().m_name == "Brand");
    require_undoable(history, std::make_unique<MoveAssetsCommand>(
      std::vector{blue}, id));
    REQUIRE(history.get_undo_label() == "Set Order in Group");
    REQUIRE(list_assets(resources, id).front() ==
      AssetEntry(std::string(), blue));
    REQUIRE(resources.m_asset_groups.front().m_elements.size() == 3);
    require_undoable(history, std::make_unique<MoveAssetsCommand>(
      std::vector{red}, COLOR_SECTION));
    REQUIRE(find_asset_group(resources, red) == COLOR_SECTION);
    REQUIRE(resources.m_asset_groups.front().m_elements ==
      std::vector{to_string(green), to_string(blue)});
    require_undoable(history, std::make_unique<RemoveSwatchCommand>(green));
    REQUIRE(resources.m_asset_groups.front().m_elements ==
      std::vector{to_string(blue)});
    auto add = std::make_unique<AddAssetGroupCommand>(id);
    auto subgroup = add->get_group();
    require_undoable(history, std::move(add));
    REQUIRE(list_assets(resources, id).front() ==
      AssetEntry(subgroup, NodeId()));
    REQUIRE(history.execute(std::make_unique<MoveAssetsCommand>(
      std::vector{blue}, subgroup)));
    require_undoable(history,
      std::make_unique<RemoveAssetGroupCommand>(subgroup, false));
    REQUIRE(history.get_undo_label() == "Delete Asset Group");
    REQUIRE(find_asset_group(resources, blue) == id);
    REQUIRE(resources.m_asset_groups.size() == 1);
    require_undoable(history,
      std::make_unique<RemoveAssetGroupCommand>(id, true));
    REQUIRE(resources.m_asset_groups.empty());
    REQUIRE(find_section(resources, blue).empty());
    REQUIRE(find_section(resources, red) == COLOR_SECTION);
  }

  TEST_CASE("component_groups") {
    auto document = Document();
    auto scene = build_scene(document);
    auto& resources = document.get_resources();
    REQUIRE(find_section(resources, scene.m_master) == COMPONENT_SECTION);
    auto file = [&] (const std::vector<std::string>& path) {
      auto transaction = Transaction(document);
      file_asset(document, transaction, scene.m_master, path);
      transaction.commit();
    };
    file({"Forms", "Inputs"});
    REQUIRE(resources.m_asset_groups.size() == 2);
    REQUIRE(get_group_path(resources, scene.m_master) ==
      std::vector<std::string>({"Forms", "Inputs"}));
    auto inputs = find_asset_group(resources, scene.m_master);
    auto& placements =
      resources.m_components.at(scene.m_component).m_placements;
    REQUIRE(placements == std::vector{AssetPlacement(inputs, "h")});
    REQUIRE(list_assets(resources, inputs) ==
      std::vector{AssetEntry(std::string(), scene.m_master)});
    auto forms = list_assets(resources, COMPONENT_SECTION).front().m_group;
    REQUIRE(list_assets(resources, forms) ==
      std::vector{AssetEntry(inputs, NodeId())});
    file({"Forms"});
    REQUIRE(resources.m_asset_groups.size() == 1);
    REQUIRE(resources.m_asset_groups.front().m_id == forms);
    REQUIRE(resources.m_asset_groups.front().m_children.empty());
    REQUIRE(resources.m_asset_groups.front().m_elements ==
      std::vector{to_string(scene.m_master)});
    file({});
    REQUIRE(resources.m_asset_groups.empty());
    REQUIRE(find_asset_group(resources, scene.m_master) == COMPONENT_SECTION);
    auto history = History(document);
    auto group = std::make_unique<GroupAssetsCommand>(
      std::vector{scene.m_master});
    auto id = group->get_group();
    require_undoable(history, std::move(group));
    REQUIRE(get_group_path(resources, scene.m_master) ==
      std::vector<std::string>({"New group"}));
    require_undoable(history, std::make_unique<MoveAssetsCommand>(
      std::vector{scene.m_master}, COMPONENT_SECTION));
    REQUIRE(get_group_path(resources, scene.m_master).empty());
    REQUIRE(history.execute(std::make_unique<MoveAssetsCommand>(
      std::vector{scene.m_master}, id)));
    auto deletion = require_undoable(history,
      std::make_unique<RemoveAssetGroupCommand>(
        id, std::vector{scene.m_instance}));
    REQUIRE(history.get_undo_label() == "Delete Asset Group");
    REQUIRE(resources.m_asset_groups.empty());
    REQUIRE(resources.m_components.empty());
    REQUIRE(!document.find(scene.m_master));
    REQUIRE(!document.find(scene.m_instance));
    auto replacement = deletion->get_selection()->front();
    auto& instance = document.get(replacement);
    REQUIRE(instance.m_parent == scene.m_artboard);
    REQUIRE(instance.get_group());
    REQUIRE(!instance.get_group()->m_component_instance);
    REQUIRE(!document.get(instance.m_children.front()).get_reference());
  }

  TEST_CASE("instances") {
    auto document = Document();
    auto scene = build_scene(document);
    auto swatch = ColorSwatch(generate_node_id(), "", make_color(255, 0, 0));
    auto instances = find_swatch_instances(document, swatch);
    REQUIRE(std::find(instances.begin(), instances.end(), scene.m_rectangle) !=
      instances.end());
    REQUIRE(std::find(instances.begin(), instances.end(), scene.m_loose) !=
      instances.end());
    REQUIRE(std::find(instances.begin(), instances.end(),
      scene.m_background) == instances.end());
    auto history = History(document);
    REQUIRE(history.execute(std::make_unique<SetVisibilityCommand>(
      std::vector{scene.m_rectangle}, false)));
    instances = find_swatch_instances(document, swatch);
    REQUIRE(std::find(instances.begin(), instances.end(), scene.m_rectangle) ==
      instances.end());
    auto style = make_character_style(*document.get(scene.m_text).get_text());
    REQUIRE(find_character_style_instances(document, style) ==
      std::vector{scene.m_text});
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
    REQUIRE(history.get_undo_label() == "Rename Color");
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
    REQUIRE(history.get_undo_label() == "Delete Color");
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
    REQUIRE(history.get_undo_label() == "Add Colors to Assets");
    auto& swatches = document.get_resources().m_swatches;
    REQUIRE(swatches.size() == 2);
    REQUIRE(swatches[0].m_color == make_color(255, 0, 0));
    REQUIRE(swatches[1].m_color == make_color(0, 0, 0));
    REQUIRE(list_assets(document.get_resources(), COLOR_SECTION) ==
      std::vector{AssetEntry(std::string(), swatches[1].m_id),
        AssetEntry(std::string(), swatches[0].m_id)});
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
    REQUIRE(history.get_undo_label() == "Add Character Style to Assets");
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
