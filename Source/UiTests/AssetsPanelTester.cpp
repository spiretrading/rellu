#include <memory>
#include <vector>
#include <doctest/doctest.h>
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
      std::vector<QString>({"#F5F5F5", "Accent"}));
    REQUIRE(panel.get_entries(AssetSection::CHARACTER_STYLES) ==
      std::vector<QString>({QString("Roboto %1 12pt").arg(EM_DASH),
        QString("Roboto Bold %1 14pt").arg(EM_DASH)}));
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
    REQUIRE(actions.size() == 3);
    REQUIRE(actions[0]->text() == "Edit Main Component");
    REQUIRE(actions[1]->text() == "Swap Component");
    REQUIRE(actions[2]->text() == "Rename");
    REQUIRE(actions[0]->isEnabled());
    REQUIRE(!actions[1]->isEnabled());
    REQUIRE(actions[2]->isEnabled());
    actions[0]->trigger();
    REQUIRE(edited == std::vector{ids.m_component});
    REQUIRE(panel.rename_component(ids.m_component, "Primary"));
    REQUIRE(document.get_resources().m_components.at(ids.m_component).
      m_name == "Primary");
    REQUIRE(panel.get_entries(AssetSection::COMPONENTS) ==
      std::vector<QString>({"Primary"}));
    REQUIRE(!panel.rename_component(ids.m_component, " "));
    REQUIRE(!panel.swap_component(ids.m_component));
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
}
