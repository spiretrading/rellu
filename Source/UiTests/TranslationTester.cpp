#include <doctest/doctest.h>
#include <QWidget>
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "../Ui/HyperlinkStatus.hpp"

using namespace Rellu;

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

  TEST_CASE("hyperlink_messages") {
    auto scope = LanguageScope(Language::SPANISH);
    REQUIRE(get_hyperlink_message(HyperlinkValidity::INVALID_DOMAIN) ==
      "La URL incluye un dominio no v\xC3\xA1lido");
    REQUIRE(get_hyperlink_message(HyperlinkValidity::VALID).isEmpty());
  }
}
