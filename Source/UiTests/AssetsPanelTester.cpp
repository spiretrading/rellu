#include <algorithm>
#include <memory>
#include <vector>
#include <doctest/doctest.h>
#include <QColor>
#include <QImage>
#include "Rellu/Commands/AssetCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Ui/AssetsPanel.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const auto EM_DASH = QString(QChar(0x2014));
}

TEST_SUITE("AssetsPanel") {
  TEST_CASE("empty_document") {
    auto document = Document();
    auto panel = AssetsPanel();
    REQUIRE(panel.is_empty_shown());
    panel.set_document(&document);
    REQUIRE(panel.is_empty_shown());
    REQUIRE(panel.get_entries(AssetSection::COLORS).empty());
  }

  TEST_CASE("entries") {
    auto document = Document();
    build_test_document(document);
    auto panel = AssetsPanel();
    panel.set_document(&document);
    REQUIRE(!panel.is_empty_shown());
    REQUIRE(panel.get_entries(AssetSection::COMPONENTS) ==
      std::vector<QString>({"Button"}));
    REQUIRE(panel.get_entries(AssetSection::COLORS).empty());
    {
      auto transaction = Transaction(document);
      auto plain = ColorSwatch();
      plain.m_color = make_color(0xF5, 0xF5, 0xF5);
      transaction.add_swatch(plain);
      auto accent = ColorSwatch();
      accent.m_name = "Accent";
      accent.m_color = make_color(0, 0, 255);
      transaction.add_swatch(accent);
      auto style = CharacterStyle();
      style.m_style.m_font = Font("Roboto", "Regular", "Roboto-Regular", 12);
      transaction.add_character_style(style);
      auto bold = CharacterStyle();
      bold.m_style.m_font = Font("Roboto", "Bold", "Roboto-Bold", 14);
      transaction.add_character_style(bold);
      transaction.commit();
    }
    REQUIRE(panel.get_entries(AssetSection::COLORS) ==
      std::vector<QString>({"Accent", "#F5F5F5"}));
    REQUIRE(panel.get_entries(AssetSection::CHARACTER_STYLES) ==
      std::vector<QString>({QString("Roboto Bold %1 14pt").arg(EM_DASH),
        QString("Roboto %1 12pt").arg(EM_DASH)}));
    panel.set_document(nullptr);
    REQUIRE(panel.get_entries(AssetSection::COMPONENTS).empty());
    REQUIRE(panel.is_empty_shown());
  }

  TEST_CASE("component_thumbnails") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto panel = AssetsPanel();
    auto renders = 0;
    panel.set_thumbnailer([&] (const auto&, const auto& component, int size) {
      ++renders;
      REQUIRE(component == ids.m_component);
      auto image = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::blue);
      return image;
    });
    panel.set_document(&document);
    REQUIRE(panel.get_components() == std::vector{ids.m_component});
    REQUIRE(panel.get_thumbnail(ids.m_component).isNull());
    panel.render_thumbnails();
    REQUIRE(renders == 1);
    REQUIRE(!panel.get_thumbnail(ids.m_component).isNull());
    {
      auto transaction = Transaction(document);
      auto header = document.get(ids.m_master_shape).m_header;
      header.m_opacity = 0.5;
      transaction.replace_header(header);
      transaction.commit();
    }
    panel.render_thumbnails();
    REQUIRE(renders == 2);
    {
      auto transaction = Transaction(document);
      auto header = document.get(ids.m_ellipse).m_header;
      header.m_opacity = 0.5;
      transaction.replace_header(header);
      transaction.commit();
    }
    panel.render_thumbnails();
    REQUIRE(renders == 2);
  }

  TEST_CASE("component_drag") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto panel = AssetsPanel();
    panel.set_document(&document);
    REQUIRE(get_component_drop_action(NodeId()) == Qt::CopyAction);
    REQUIRE(get_component_drop_action(ids.m_instance) == SWAP_DROP_ACTION);
    REQUIRE(make_drag_cursor(Qt::CopyAction).toImage() ==
      make_arrow_cursor().toImage());
    REQUIRE(make_drag_cursor(SWAP_DROP_ACTION).toImage() ==
      make_swap_cursor().toImage());
    auto arrow = make_arrow_cursor().toImage();
    REQUIRE(arrow.pixelColor(3, 8) == QColor(Qt::white));
    REQUIRE(arrow.pixelColor(0, 4).alpha() == 255);
    REQUIRE(arrow.pixelColor(25, 25).alpha() == 0);
    auto pixmap = panel.make_drag_pixmap(ids.m_component);
    REQUIRE(!pixmap.isNull());
    REQUIRE(pixmap.deviceIndependentSize().height() == 38);
    REQUIRE(pixmap.deviceIndependentSize().width() > 38);
    auto image = pixmap.toImage();
    REQUIRE(image.pixelColor(2, 2) == QColor(0xE0, 0xF0, 0xFA));
    auto has_text = false;
    for(auto x = 60; x != image.width() && !has_text; ++x) {
      for(auto y = 0; y != image.height() && !has_text; ++y) {
        has_text = image.pixelColor(x, y).lightness() < 128;
      }
    }
    REQUIRE(has_text);
  }

  TEST_CASE("component_actions") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = AssetsPanel();
    auto edited = std::vector<ComponentId>();
    panel.set_main_component_handler([&] (const auto& component) {
      edited.push_back(component);
    });
    panel.set_editor(&editor);
    REQUIRE(panel.get_document() == &document);
    auto menu = std::unique_ptr<QMenu>(
      panel.make_component_menu(ids.m_component, nullptr));
    auto actions = menu->actions();
    actions.removeIf([] (auto action) {
      return action->isSeparator();
    });
    REQUIRE(actions.size() == 5);
    REQUIRE(actions[0]->text() == "Edit Main Component");
    REQUIRE(actions[1]->text() == "New group from element");
    REQUIRE(actions[2]->text() == "Move to...");
    REQUIRE(actions[3]->text() == "Rename");
    REQUIRE(actions[4]->text() == "Delete");
    for(auto action : actions) {
      REQUIRE(action->isEnabled());
    }
    actions[0]->trigger();
    REQUIRE(edited == std::vector{ids.m_component});
    REQUIRE(panel.rename_component(ids.m_component, "Primary"));
    REQUIRE(document.get_resources().m_components.at(ids.m_component).
      m_name == "Primary");
    REQUIRE(panel.get_entries(AssetSection::COMPONENTS) ==
      std::vector<QString>({"Primary"}));
    REQUIRE(!panel.rename_component(ids.m_component, " "));
    auto group = panel.group_assets({ids.m_master});
    REQUIRE(!group.empty());
    REQUIRE(panel.get_group_names(AssetSection::COMPONENTS) ==
      std::vector<QString>({"New group"}));
    REQUIRE(panel.rename_group(group, "Forms"));
    REQUIRE(panel.get_group_names(AssetSection::COMPONENTS) ==
      std::vector<QString>({"Forms"}));
    REQUIRE(panel.get_components() == std::vector{ids.m_component});
    menu.reset(panel.make_group_menu(group, nullptr));
    REQUIRE(menu->actions().back()->isEnabled());
    auto asked = std::vector<QString>();
    auto answer = AssetConfirmation();
    panel.set_confirmation_handler([&] (const QString& title,
        const QString& message, const QString& ok, const QString& cancel,
        const QString& dismissal) {
      asked = {title, message, ok, cancel, dismissal};
      return answer;
    });
    REQUIRE(!panel.delete_group(group));
    REQUIRE(asked == std::vector<QString>({"Delete group and contents",
      "This will delete the group and any subgroups or elements that it "
      "contains. To remove the group without deleting its elements, use the "
      "Ungroup action.", "Delete", "Cancel", "Do not show this again"}));
    REQUIRE(!document.get_resources().m_asset_groups.empty());
    answer = AssetConfirmation(true, true);
    editor.get_selection().select(ids.m_instance);
    REQUIRE(panel.delete_group(group));
    REQUIRE(!panel.is_group_deletion_confirmed());
    REQUIRE(document.get_resources().m_components.empty());
    REQUIRE(document.get_resources().m_asset_groups.empty());
    REQUIRE(!document.find(ids.m_instance));
    auto selected = editor.get_selection().get_nodes();
    REQUIRE(selected.size() == 1);
    REQUIRE(!get_instance_role(document.get(selected.front())));
    REQUIRE(editor.get_history().get_undo_label() == "Delete Asset Group");
    REQUIRE(editor.get_history().undo());
    REQUIRE(get_instance_role(document.get(ids.m_instance)));
    REQUIRE(panel.get_components() == std::vector{ids.m_component});
    panel.set_removal_limit(0);
    answer = AssetConfirmation();
    REQUIRE(!panel.delete_components({ids.m_component}));
    REQUIRE(asked == std::vector<QString>({"Permanently delete",
      "Are you sure you want to delete 1 items? This cannot be undone.",
      "Delete", "Cancel", QString()}));
    REQUIRE(panel.get_components() == std::vector{ids.m_component});
    asked.clear();
    answer = AssetConfirmation(true, false);
    REQUIRE(panel.delete_group(panel.group_assets({ids.m_master})));
    REQUIRE(asked.front() == "Permanently delete");
    REQUIRE(editor.get_history().undo());
    panel.set_removal_limit(2000);
    REQUIRE(panel.delete_components({ids.m_component}));
    REQUIRE(editor.get_history().get_undo_label() == "Delete Components");
    REQUIRE(panel.get_components().empty());
  }

  TEST_CASE("colors") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = AssetsPanel();
    panel.set_editor(&editor);
    REQUIRE(!panel.add_colors());
    editor.get_selection().select(ids.m_background);
    REQUIRE(panel.add_colors());
    REQUIRE(panel.get_entries(AssetSection::COLORS) ==
      std::vector<QString>({"#FF0000"}));
    REQUIRE(!panel.add_colors());
    auto swatch = panel.get_swatches().front();
    editor.get_selection().select(ids.m_ellipse);
    REQUIRE(panel.apply_swatch(swatch, false));
    REQUIRE(document.get(ids.m_ellipse).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(255, 0, 0), swatch)));
    REQUIRE(panel.apply_swatch(swatch, true));
    REQUIRE(document.get(ids.m_ellipse).get_shape()->m_style.m_stroke->
      m_color == make_color(255, 0, 0));
    auto edited = document.get_resources().m_swatches.front();
    edited.m_color = make_color(0, 255, 0);
    REQUIRE(panel.replace_swatch(edited));
    REQUIRE(document.get(ids.m_ellipse).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 255, 0), swatch)));
    REQUIRE(panel.rename_swatch(swatch, "Primary"));
    REQUIRE(panel.get_entries(AssetSection::COLORS) ==
      std::vector<QString>({"Primary"}));
    auto menu = std::unique_ptr<QMenu>(panel.make_swatch_menu(swatch, nullptr));
    auto texts = QStringList();
    for(auto action : menu->actions()) {
      if(!action->isSeparator()) {
        texts.push_back(action->text());
      }
    }
    REQUIRE(texts == QStringList({"Copy #00FF00", "Edit",
      "Highlight on canvas", "New group from element", "Move to...",
      "Rename", "Delete"}));
    REQUIRE(panel.delete_swatch(swatch));
    REQUIRE(panel.get_swatches().empty());
    REQUIRE(document.get(ids.m_ellipse).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 255, 0))));
  }

  TEST_CASE("asset_groups") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = AssetsPanel();
    panel.set_editor(&editor);
    editor.get_selection().select(ids.m_background);
    REQUIRE(panel.add_colors());
    REQUIRE(editor.execute(std::make_unique<AddSwatchCommand>(
      ColorSwatch(NodeId(), "", make_color(0, 255, 0)))));
    auto swatches = panel.get_swatches();
    REQUIRE(swatches.size() == 2);
    auto group = panel.group_assets({swatches.back()});
    REQUIRE(!group.empty());
    REQUIRE(editor.get_history().get_undo_label() == "Create Asset Group");
    REQUIRE(panel.get_group_names(AssetSection::COLORS) ==
      std::vector<QString>({"New group"}));
    REQUIRE(panel.rename_group(group, "Brand"));
    REQUIRE(panel.get_group_names(AssetSection::COLORS) ==
      std::vector<QString>({"Brand"}));
    auto group_menu =
      std::unique_ptr<QMenu>(panel.make_group_menu(group, nullptr));
    auto group_texts = QStringList();
    for(auto action : group_menu->actions()) {
      if(!action->isSeparator()) {
        group_texts.push_back(action->text());
      }
    }
    REQUIRE(group_texts ==
      QStringList({"Create subgroup", "Rename group", "Ungroup", "Delete"}));
    REQUIRE(panel.move_assets({swatches.front()}, group));
    REQUIRE(find_asset_group(document.get_resources(), swatches.front()) ==
      group);
    panel.set_selected_assets(swatches);
    auto menu = std::unique_ptr<QMenu>(panel.make_selection_menu(nullptr));
    auto texts = QStringList();
    for(auto action : menu->actions()) {
      if(!action->isSeparator()) {
        texts.push_back(action->text());
      }
    }
    REQUIRE(texts == QStringList({"New group from selection",
      "Move selected items to", "Delete"}));
    REQUIRE(panel.delete_assets(swatches));
    REQUIRE(editor.get_history().get_undo_label() == "Delete Colors");
    REQUIRE(panel.get_swatches().empty());
  }

  TEST_CASE("highlight_on_canvas") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = AssetsPanel();
    auto highlighted = std::vector<NodeId>();
    auto messages = QStringList();
    panel.set_highlight_handler([&] (const auto& nodes) {
      highlighted = nodes;
    });
    panel.set_message_handler([&] (const auto& message) {
      messages.push_back(message);
    });
    panel.set_editor(&editor);
    editor.get_selection().select(ids.m_background);
    REQUIRE(panel.add_colors());
    auto swatch = panel.get_swatches().front();
    REQUIRE(panel.highlight_instances(swatch));
    REQUIRE(std::find(highlighted.begin(), highlighted.end(),
      ids.m_background) != highlighted.end());
    REQUIRE(editor.get_selection().is_empty());
    auto unused = Transaction(document);
    auto id = unused.add_swatch(
      ColorSwatch(NodeId(), "", make_color(1, 2, 3)));
    unused.commit();
    REQUIRE(!panel.highlight_instances(id));
    REQUIRE(highlighted.empty());
    REQUIRE(messages == QStringList({"0 objects use this color"}));
  }

  TEST_CASE("color_editing") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = AssetsPanel();
    panel.set_editor(&editor);
    editor.get_selection().select(ids.m_background);
    REQUIRE(panel.add_colors());
    auto swatch = panel.get_swatches().front();
    auto count = editor.get_history().get_undo_count();
    panel.edit_swatch(swatch);
    auto& picker = panel.get_color_picker();
    REQUIRE(picker.isVisible());
    picker.apply_color(make_color(0, 0, 255));
    picker.apply_color(make_color(0, 0, 128));
    picker.hide();
    REQUIRE(editor.get_history().get_undo_count() == count + 1);
    REQUIRE(editor.get_history().get_undo_label() == "Edit Color");
    REQUIRE(document.get_resources().m_swatches.front().m_color ==
      make_color(0, 0, 128));
  }

  TEST_CASE("character_styles") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = AssetsPanel();
    panel.set_editor(&editor);
    editor.get_selection().select(ids.m_label);
    REQUIRE(panel.add_character_styles());
    REQUIRE(panel.get_entries(AssetSection::CHARACTER_STYLES) ==
      std::vector<QString>({QString("Roboto Medium %1 14pt").arg(EM_DASH)}));
    auto style = panel.get_character_styles().front();
    auto edited = document.get_resources().m_character_styles.front();
    edited.m_style.m_font.m_size = 20;
    REQUIRE(panel.replace_character_style(edited));
    REQUIRE(panel.apply_character_style(style));
    auto& text = *document.get(ids.m_label).get_text();
    REQUIRE(text.m_runs.front().m_character_style == style);
    REQUIRE(text.m_runs.front().m_style.m_font.m_size == 20);
    REQUIRE(panel.rename_character_style(style, "Body"));
    REQUIRE(document.get_resources().m_character_styles.front().m_name ==
      "Body");
    auto menu = std::unique_ptr<QMenu>(
      panel.make_character_style_menu(style, nullptr));
    REQUIRE(menu->actions().front()->text() == "Remove font");
    REQUIRE(!menu->actions().front()->isEnabled());
    REQUIRE(panel.delete_character_style(style));
    REQUIRE(panel.get_character_styles().empty());
  }

  TEST_CASE("component_mime_data") {
    auto component = generate_node_id();
    auto data = std::unique_ptr<QMimeData>(make_component_mime_data(component));
    REQUIRE(data->hasFormat(COMPONENT_MIME_TYPE));
    REQUIRE(read_component_mime_data(*data) == component);
    auto other = QMimeData();
    other.setText("text");
    REQUIRE(!read_component_mime_data(other));
  }

  TEST_CASE("swap_cursor") {
    auto cursor = make_swap_cursor().toImage();
    REQUIRE(cursor.size() == QSize(32, 32));
    REQUIRE(cursor.pixelColor(2, 6) == QColor(Qt::black));
    REQUIRE(cursor.pixelColor(31, 0).alpha() == 0);
    REQUIRE(cursor.pixelColor(21, 17).alpha() > 0);
  }
}
