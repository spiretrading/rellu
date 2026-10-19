#include <memory>
#include <doctest/doctest.h>
#include "Rellu/Commands/AssetCommands.hpp"
#include "Rellu/Ui/AssetMoveDialog.hpp"

using namespace Rellu;

namespace {
  SwatchId add_swatch(Editor& editor) {
    auto swatch = ColorSwatch();
    swatch.m_id = generate_node_id();
    swatch.m_name = "Red";
    swatch.m_color = Color(1, 0, 0, 1);
    editor.execute(std::make_unique<AddSwatchCommand>(swatch));
    return swatch.m_id;
  }

  std::string add_group(Editor& editor, std::string name) {
    auto command = std::make_unique<AddAssetGroupCommand>(
      COLOR_SECTION, std::move(name));
    auto group = command->get_group();
    editor.execute(std::move(command));
    return group;
  }
}

TEST_SUITE("AssetMoveDialog") {
  TEST_CASE("navigation") {
    auto document = Document();
    auto editor = Editor(document);
    auto swatch = add_swatch(editor);
    auto brand = add_group(editor, "Brand");
    auto dialog = AssetMoveDialog(editor, {swatch});
    REQUIRE(dialog.get_title() == "Move to");
    REQUIRE(dialog.get_group() == COLOR_SECTION);
    REQUIRE(dialog.get_location() == "Colors");
    REQUIRE(dialog.get_entries().size() == 1);
    REQUIRE(dialog.get_entries().front().m_name == "Brand");
    REQUIRE(dialog.can_submit());
    REQUIRE(dialog.can_go_back());
    dialog.go_back();
    REQUIRE(dialog.get_group().empty());
    REQUIRE(dialog.get_location() == "Document Assets");
    REQUIRE(!dialog.can_go_back());
    REQUIRE(!dialog.can_submit());
    auto entries = dialog.get_entries();
    REQUIRE(entries.size() == 3);
    REQUIRE(entries[0].m_is_enabled);
    REQUIRE(!entries[1].m_is_enabled);
    REQUIRE(!entries[2].m_is_enabled);
    REQUIRE(!dialog.open(CHARACTER_STYLE_SECTION));
    REQUIRE(dialog.open(COLOR_SECTION));
    REQUIRE(dialog.open(brand));
    REQUIRE(dialog.get_location() == "Brand");
    REQUIRE(dialog.get_entries().empty());
    REQUIRE(dialog.submit());
    REQUIRE(find_asset_group(document.get_resources(), swatch) == brand);
  }

  TEST_CASE("create_group") {
    auto document = Document();
    auto editor = Editor(document);
    auto swatch = add_swatch(editor);
    add_group(editor, "Brand");
    add_group(editor, "New group");
    auto dialog = AssetMoveDialog(editor, {swatch});
    dialog.start_creating();
    REQUIRE(dialog.is_creating());
    REQUIRE(dialog.get_title() == "Create new group");
    REQUIRE(dialog.can_submit());
    dialog.set_name("brand");
    REQUIRE(!dialog.can_submit());
    dialog.set_name("a/b");
    REQUIRE(!dialog.can_submit());
    dialog.set_name(" ");
    REQUIRE(!dialog.can_submit());
    dialog.cancel_creating();
    REQUIRE(!dialog.is_creating());
    dialog.start_creating();
    dialog.set_name("Logos");
    REQUIRE(dialog.submit());
    REQUIRE(!dialog.is_creating());
    REQUIRE(dialog.get_location() == "Logos");
    REQUIRE(editor.get_history().get_undo_label() == "Create Asset Group");
    auto logos = dialog.get_group();
    REQUIRE(dialog.submit());
    REQUIRE(find_asset_group(document.get_resources(), swatch) == logos);
  }

  TEST_CASE("default_name") {
    auto document = Document();
    auto editor = Editor(document);
    auto swatch = add_swatch(editor);
    add_group(editor, "New group");
    auto dialog = AssetMoveDialog(editor, {swatch});
    dialog.start_creating();
    REQUIRE(dialog.submit());
    REQUIRE(dialog.get_location() == "New group\xC2\xA0" "1");
  }
}
