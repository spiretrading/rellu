#include <optional>
#include <vector>
#include <doctest/doctest.h>
#include <QLabel>
#include "Rellu/Ui/ArtboardPresetsPanel.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("ArtboardPresetsPanel") {
  TEST_CASE("rows") {
    auto panel = ArtboardPresetsPanel();
    auto rows = panel.findChildren<QWidget*>("artboard_preset");
    REQUIRE(rows.size() == 34);
    REQUIRE(rows.front()->accessibleName() == "iPhone 14 Pro Max");
    auto labels = rows.front()->findChildren<QLabel*>();
    REQUIRE(labels.size() == 2);
    REQUIRE(labels[1]->text() == "430 x 932");
    auto headings = QStringList();
    for(auto heading : panel.findChildren<QLabel*>("section_heading")) {
      headings.append(heading->text());
    }
    REQUIRE(headings == QStringList(
      {"MOBILE", "TABLET", "WEB/DESKTOP", "SOCIAL MEDIA", "WATCH"}));
  }

  TEST_CASE("create") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = ArtboardPresetsPanel();
    auto web = get_artboard_presets()[2].m_presets[0];
    REQUIRE(panel.create(web).is_nil());
    panel.set_editor(&editor);
    panel.set_view_provider([] {
      return std::optional(Rect(0, 0, 1000, 900));
    });
    auto revealed = std::optional<Rect>();
    panel.set_reveal_handler([&] (const auto& region) {
      revealed = region;
    });
    editor.get_selection().select(ids.m_artboard);
    editor.get_context().enter(ids.m_card);
    auto artboard = panel.create(web);
    REQUIRE(!artboard.is_nil());
    REQUIRE(editor.get_selection().get_nodes() == std::vector{artboard});
    REQUIRE(editor.get_context().get_container().is_nil());
    auto& frame = document.get(artboard).get_artboard()->m_frame;
    REQUIRE(frame == Rect(545, 50, 1920, 1080));
    REQUIRE(revealed == frame);
    REQUIRE(document.get(artboard).m_header.m_name ==
      "Web 1920 \xE2\x80\x93 1");
    REQUIRE(get_last_used_preset().m_name == "Web 1920");
    set_last_used_preset(get_artboard_presets()[0].m_presets[1]);
  }

  TEST_CASE("footer") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    inspector.set_editor(&editor);
    auto panel = new ArtboardPresetsPanel();
    inspector.set_footer(panel, true);
    REQUIRE(inspector.get_sections().empty());
    REQUIRE(panel->isVisibleTo(&inspector));
    editor.get_selection().select(ids.m_artboard);
    inspector.set_footer(panel, false);
    REQUIRE(!inspector.get_sections().empty());
    REQUIRE(panel->isVisibleTo(&inspector));
    inspector.set_footer(nullptr, false);
    REQUIRE(!panel->isVisibleTo(&inspector));
    REQUIRE(panel->parent() == &inspector);
  }
}
