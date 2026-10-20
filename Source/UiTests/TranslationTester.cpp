#include <memory>
#include <doctest/doctest.h>
#include <QWidget>
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/AssetMoveDialog.hpp"
#include "Rellu/Ui/EditActions.hpp"
#include "Rellu/Ui/GuideActions.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "../Ui/HyperlinkStatus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct LanguageScope {
    explicit LanguageScope(Language language) {
      set_language(language);
    }

    ~LanguageScope() {
      set_language(Language::ENGLISH);
    }
  };
}

TEST_SUITE("Translation") {
  TEST_CASE("translate") {
    REQUIRE(translate("menu", "&File") == "&File");
    auto scope = LanguageScope(Language::GERMAN);
    REQUIRE(translate("menu", "&File") == "Datei");
    REQUIRE(translate("inspector.section", "Appearance") ==
      QString::fromUtf8("ERSCHEINUNGSBILD"));
    REQUIRE(translate("Unknown text") == "Unknown text");
    REQUIRE(translate("menu", QString()).isEmpty());
  }

  TEST_CASE("menus") {
    auto scope = LanguageScope(Language::FRENCH);
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    REQUIRE(actions.get("edit.copy").text() == "Copier");
    REQUIRE(actions.get("object.scroll_group.panning").text() ==
      "Horizontal et vertical");
    REQUIRE(actions.get("edit.preferences.language.german").text() ==
      "Deutsch");
  }

  TEST_CASE("guide_menus") {
    auto scope = LanguageScope(Language::GERMAN);
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    auto guides = GuideActions(actions);
    REQUIRE(actions.get("view.guides.hide").text() ==
      "Alle Hilfslinien ausblenden");
    REQUIRE(actions.get("view.guides.lock").text() ==
      "Alle Hilfslinien sperren");
    auto settings = GuideSettings();
    settings.m_is_visible = false;
    settings.m_is_locked = true;
    guides.set_settings(settings);
    REQUIRE(actions.get("view.guides.hide").text() ==
      "Alle Hilfslinien einblenden");
    REQUIRE(actions.get("view.guides.lock").text() ==
      "Alle Hilfslinien entsperren");
  }

  TEST_CASE("undo_labels") {
    auto scope = LanguageScope(Language::GERMAN);
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    auto commands = CommandRegistry();
    add_standard_commands(commands);
    auto edit_actions = EditActions(actions, commands);
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    edit_actions.set_editor(&editor);
    editor.execute(std::make_unique<InsertNodeCommand>("Draw Rectangle",
      ids.m_artboard, make_header(""), make_rectangle_shape(10, 10)));
    REQUIRE(actions.get("edit.undo").text() == QString::fromUtf8(
      "\xE2\x80\x9ERechteck zeichnen\xE2\x80\x9C r\xC3\xBC" "ckg"
      "\xC3\xA4ngig machen"));
  }

  TEST_CASE("asset_move_dialog") {
    auto scope = LanguageScope(Language::GERMAN);
    auto document = Document();
    auto editor = Editor(document);
    auto dialog = AssetMoveDialog(editor, {});
    REQUIRE(dialog.get_title() == "Verschieben in");
    REQUIRE(dialog.get_location() == "Dokumentelemente");
    REQUIRE(dialog.get_entries().front().m_name == "Farben");
  }

  TEST_CASE("hyperlink_messages") {
    auto scope = LanguageScope(Language::SPANISH);
    REQUIRE(get_hyperlink_message(HyperlinkValidity::INVALID_DOMAIN) ==
      "La URL incluye un dominio no v\xC3\xA1lido");
    REQUIRE(get_hyperlink_message(HyperlinkValidity::VALID).isEmpty());
  }
}
