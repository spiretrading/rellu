#include <algorithm>
#include <ostream>
#include <string_view>
#include <tuple>
#include <doctest/doctest.h>
#include "Rellu/Localization/Localization.hpp"
#include "../Localization/Translations.hpp"

using namespace Rellu;

TEST_SUITE("Localization") {
  TEST_CASE("codes") {
    for(auto language : LANGUAGES) {
      REQUIRE(parse_language(get_code(language)) == language);
    }
    REQUIRE(get_code(Language::GERMAN) == "german");
    REQUIRE(get_code(Language::CHINESE) == "chinese");
    REQUIRE(!parse_language("klingon"));
  }

  TEST_CASE("native_names") {
    REQUIRE(get_native_name(Language::ENGLISH) == "English");
    REQUIRE(get_native_name(Language::SPANISH) == "Espa\xC3\xB1ol");
    REQUIRE(get_native_name(Language::RUSSIAN) ==
      "P\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9");
  }

  TEST_CASE("localize") {
    REQUIRE(localize("menu", "&File", Language::ENGLISH) == "&File");
    REQUIRE(localize("menu", "&File", Language::GERMAN) == "Datei");
    REQUIRE(localize("menu.recent", "&File", Language::GERMAN) == "Datei");
    REQUIRE(localize("", "Not a user interface string", Language::GERMAN) ==
      "Not a user interface string");
    REQUIRE(localize("prototype.easing", "None", Language::GERMAN) == "Aus");
    REQUIRE(localize("prototype.animation", "None", Language::GERMAN) ==
      "Ohne");
    REQUIRE(localize("name", "Rectangle", Language::FRENCH) == "Rectangle");
    REQUIRE(localize("name", "Rectangle", Language::ITALIAN) == "Rettangolo");
    REQUIRE(localize("menu", "Undo %1", Language::SPANISH) == "Deshacer %1");
    REQUIRE(localize("history", "Outline Stroke", Language::GERMAN) ==
      "Konturlinie");
    REQUIRE(localize("history", "Draw Rectangle", Language::GERMAN) ==
      "Rechteck zeichnen");
    REQUIRE(localize("history", "Move Guide", Language::GERMAN) ==
      "Hilfslinie verschieben");
    REQUIRE(localize("menu", "Show All Guides", Language::GERMAN) ==
      "Alle Hilfslinien einblenden");
    REQUIRE(localize("name", "Outline", Language::FRENCH) == "Contour");
    REQUIRE(localize("assets", "Move to", Language::GERMAN) ==
      "Verschieben in");
    REQUIRE(localize("message",
      "You must restart Rellu to apply the language change.",
      Language::SPANISH) ==
      "Debes reiniciar Rellu para aplicar el cambio de idioma.");
  }

  TEST_CASE("language") {
    REQUIRE(get_language() == Language::ENGLISH);
    REQUIRE(localize("menu", "&Edit") == "&Edit");
    set_language(Language::GERMAN);
    REQUIRE(get_language() == Language::GERMAN);
    REQUIRE(localize("menu", "&Edit") == "Bearbeiten");
    set_language(Language::ENGLISH);
  }

  TEST_CASE("translations") {
    auto translations = get_translations();
    REQUIRE(!translations.empty());
    REQUIRE(std::ranges::adjacent_find(translations,
      [] (const auto& left, const auto& right) {
        return std::tuple(left.m_context, left.m_text) >=
          std::tuple(right.m_context, right.m_text);
      }) == translations.end());
    for(auto& translation : translations) {
      for(auto& text : translation.m_translations) {
        for(auto marker : {"%1", "%2"}) {
          REQUIRE((translation.m_text.find(marker) == std::string_view::npos ||
            text.empty() || text.find(marker) != std::string_view::npos));
        }
      }
    }
  }
}
