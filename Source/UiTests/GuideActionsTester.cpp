#include <memory>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QWidget>
#include "Rellu/Ui/GuideActions.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Fixture {
    QWidget m_window;
    ActionRegistry m_actions;
    std::unique_ptr<GuideActions> m_guide_actions;
    GuideSettings m_published;

    Fixture()
        : m_actions(m_window) {
      add_xd_menus(m_actions);
      m_guide_actions = std::make_unique<GuideActions>(m_actions);
      m_guide_actions->set_settings_handler([this] (const auto& settings) {
        m_published = settings;
      });
    }

    bool is_enabled(const std::string& name) const {
      return m_actions.get(name).isEnabled();
    }

    QString get_text(const std::string& name) const {
      return m_actions.get(name).text();
    }
  };

  void add_guides(Document& document, const NodeId& id) {
    auto artboard = *document.get(id).get_artboard();
    artboard.m_guides = {Guide(Orientation::HORIZONTAL, 30, "h"),
      Guide(Orientation::VERTICAL, 40, "v")};
    auto transaction = Transaction(document);
    transaction.replace_payload(id, std::move(artboard));
    transaction.commit();
  }

  const std::vector<Guide>& get_guides(
      const Document& document, const NodeId& id) {
    return document.get(id).get_artboard()->m_guides;
  }
}

TEST_SUITE("GuideActions") {
  TEST_CASE("toggles") {
    auto fixture = Fixture();
    auto document = Document();
    build_test_document(document);
    auto editor = Editor(document);
    REQUIRE(!fixture.is_enabled("view.guides.hide"));
    fixture.m_guide_actions->set_editor(&editor);
    REQUIRE(fixture.is_enabled("view.guides.hide"));
    REQUIRE(fixture.get_text("view.guides.hide") == "Hide All Guides");
    REQUIRE(fixture.get_text("view.guides.lock") == "Lock All Guides");
    fixture.m_actions.get("view.guides.hide").trigger();
    REQUIRE(fixture.get_text("view.guides.hide") == "Show All Guides");
    REQUIRE(!fixture.m_published.m_is_visible);
    fixture.m_actions.get("view.guides.lock").trigger();
    REQUIRE(fixture.get_text("view.guides.lock") == "Unlock All Guides");
    REQUIRE(fixture.m_published.m_is_locked);
    fixture.m_actions.get("view.guides.hide").trigger();
    REQUIRE(fixture.m_published.m_is_visible);
    fixture.m_guide_actions->set_workspace(Workspace::PROTOTYPE);
    REQUIRE(!fixture.m_published.m_is_visible);
    REQUIRE(fixture.m_guide_actions->get_settings().m_is_visible);
    REQUIRE(!fixture.is_enabled("view.guides.hide"));
  }

  TEST_CASE("undo") {
    auto fixture = Fixture();
    auto document = Document();
    build_test_document(document);
    auto editor = Editor(document);
    auto& history = editor.get_history();
    fixture.m_guide_actions->set_editor(&editor);
    fixture.m_actions.get("view.guides.hide").trigger();
    REQUIRE(history.get_undo_label() == "Hide All Guides");
    fixture.m_actions.get("view.guides.lock").trigger();
    REQUIRE(history.get_undo_label() == "Lock All Guides");
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(editor.undo());
    REQUIRE(!fixture.m_published.m_is_locked);
    REQUIRE(fixture.get_text("view.guides.lock") == "Lock All Guides");
    REQUIRE(!fixture.m_published.m_is_visible);
    REQUIRE(editor.undo());
    REQUIRE(fixture.m_published.m_is_visible);
    REQUIRE(fixture.get_text("view.guides.hide") == "Hide All Guides");
    REQUIRE(editor.redo());
    REQUIRE(!fixture.m_published.m_is_visible);
    fixture.m_actions.get("view.guides.hide").trigger();
    REQUIRE(history.get_undo_label() == "Show All Guides");
    REQUIRE(!history.can_redo());
    fixture.m_actions.get("view.guides.lock").trigger();
    fixture.m_actions.get("view.guides.lock").trigger();
    REQUIRE(history.get_undo_label() == "Unlock All Guides");
    fixture.m_guide_actions.reset();
    REQUIRE(editor.undo());
  }

  TEST_CASE("clear") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_guide_actions->set_editor(&editor);
    editor.get_selection().select(ids.m_ellipse);
    REQUIRE(!fixture.is_enabled("view.guides.clear"));
    REQUIRE(!fixture.is_enabled("view.guides.copy"));
    REQUIRE(fixture.is_enabled("view.guides.paste"));
    add_guides(document, ids.m_artboard);
    editor.get_selection().select(ids.m_artboard);
    REQUIRE(fixture.is_enabled("view.guides.clear"));
    REQUIRE(fixture.is_enabled("view.guides.copy"));
    fixture.m_actions.get("view.guides.clear").trigger();
    REQUIRE(get_guides(document, ids.m_artboard).empty());
    REQUIRE(editor.get_history().get_undo_label() == "Clear Guides");
    REQUIRE(!fixture.is_enabled("view.guides.clear"));
  }

  TEST_CASE("paste") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_guide_actions->set_editor(&editor);
    auto data = make_guides_data({Guide(Orientation::VERTICAL, 25, "a"),
      Guide(Orientation::HORIZONTAL, 900, "b")});
    REQUIRE(read_guides_data(*data)->size() == 2);
    REQUIRE(!fixture.m_guide_actions->paste(QMimeData(), true));
    fixture.m_actions.get("view.guides.hide").trigger();
    editor.get_selection().select(ids.m_ellipse);
    REQUIRE(fixture.m_guide_actions->paste(*data, true));
    REQUIRE(get_guides(document, ids.m_artboard).size() == 1);
    auto& guide = get_guides(document, ids.m_artboard).front();
    REQUIRE(guide.m_position == 25);
    REQUIRE(guide.m_id != "a");
    REQUIRE(editor.get_history().get_undo_label() == "Paste Guides");
    REQUIRE(fixture.m_guide_actions->get_settings().m_is_visible);
  }
}
