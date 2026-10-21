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
    REQUIRE(localize("history", "Star Ratio", Language::GERMAN) ==
      "Stern: Inkreis-/Umkreisverh\xC3\xA4ltnis");
    REQUIRE(localize("history", "Corner Count", Language::GERMAN) ==
      "Anzahl Ecken");
    REQUIRE(localize("history", "Corner Radius", Language::GERMAN) ==
      "Eckenradius");
    REQUIRE(localize("history", "Extend Path", Language::GERMAN) ==
      "Pfad erweitern");
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

  TEST_CASE("xd_terms") {
    REQUIRE(localize("message",
      "You must restart Rellu to apply the language change.",
      Language::JAPANESE) == "\xE8\xA8\x80\xE8\xAA\x9E\xE3\x81\xAE\xE5\xA4\x89"
      "\xE6\x9B\xB4\xE3\x82\x92\xE9\x81\xA9\xE7\x94\xA8\xE3\x81\x99\xE3\x82"
      "\x8B\xE3\x81\xAB\xE3\x81\xAF Rellu \xE3\x82\x92\xE5\x86\x8D\xE8\xB5\xB7"
      "\xE5\x8B\x95\xE3\x81\x99\xE3\x82\x8B\xE5\xBF\x85\xE8\xA6\x81\xE3\x81"
      "\x8C\xE3\x81\x82\xE3\x82\x8A\xE3\x81\xBE\xE3\x81\x99\xE3\x80\x82");
    REQUIRE(localize("message",
      "You must restart Rellu to apply the language change.",
      Language::ENGLISH) ==
      "You must restart Rellu to apply the language change.");
    REQUIRE(localize("layers", "ARTBOARDS", Language::GERMAN) ==
      "ZEICHENFL\xC3\x84" "CHEN");
    REQUIRE(localize("layers", "Pasteboard", Language::GERMAN) ==
      "Montagefl\xC3\xA4" "che");
    REQUIRE(localize("layers", "Pasteboard", Language::JAPANESE) ==
      "\xE3\x83\x9A\xE3\x83\xBC\xE3\x82\xB9\xE3\x83\x88\xE3\x83\x9C\xE3\x83"
      "\xBC\xE3\x83\x89");
    REQUIRE(localize("layers", "PASTEBOARD", Language::GERMAN) ==
      "MONTAGEFL\xC3\x84" "CHE");
    REQUIRE(localize("prototype", " s", Language::GERMAN) == " Sek.");
    REQUIRE(localize("prototype", "Ease In", Language::GERMAN) == "Ease In");
    REQUIRE(localize("prototype", "Tap", Language::GERMAN) == "Antippen");
    REQUIRE(localize("prototype", "Transition", Language::GERMAN) ==
      "\xC3\x9C" "bergang");
    REQUIRE(localize("history", "Draw Rectangle", Language::JAPANESE) ==
      "\xE9\x95\xB7\xE6\x96\xB9\xE5\xBD\xA2\xE3\x82\x92\xE6\x8F\x8F\xE7\x94"
      "\xBB");
    REQUIRE(localize("shortcut", "Shift", Language::GERMAN) == "Umschalt");
    REQUIRE(localize("shortcut", "Del", Language::JAPANESE) == "Delete");
  }

  TEST_CASE("default_names") {
    REQUIRE(localize_default_name("Rectangle 3", "SHAPE_RECTANGLE",
      Language::GERMAN) == "Rechteck 3");
    REQUIRE(localize_default_name("Rectangle 3", "SHAPE_RECTANGLE",
      Language::JAPANESE) == "\xE9\x95\xB7\xE6\x96\xB9\xE5\xBD\xA2 3");
    REQUIRE(localize_default_name("Rechteck 3", "SHAPE_RECTANGLE",
      Language::ENGLISH) == "Rectangle 3");
    REQUIRE(localize_default_name("Rectangle 3", "SHAPE_RECTANGLE",
      Language::ENGLISH) == "Rectangle 3");
    REQUIRE(localize_default_name("Component 1 \xE2\x80\x93 12",
      "SHAPE_COMPONENT", Language::GERMAN) ==
      "Komponente 1 \xE2\x80\x93 12");
    REQUIRE(localize_default_name("Gruppe maskieren 2", "SHAPE_MASK",
      Language::ENGLISH) == "Mask Group 2");
    REQUIRE(localize_default_name("Grupo 2", "SHAPE_GROUP",
      Language::FRENCH) == "Groupe 2");
    REQUIRE(localize_default_name("Rectangle", "SHAPE_RECTANGLE",
      Language::GERMAN) == "Rechteck");
    REQUIRE(localize_default_name("Box", "SHAPE_RECTANGLE",
      Language::ENGLISH) == "Rectangle");
    REQUIRE(localize_default_name("Box 3", "SHAPE_RECTANGLE",
      Language::GERMAN) == "Rechteck 3");
    REQUIRE(localize_default_name("Rectangle 1 2", "SHAPE_RECTANGLE",
      Language::GERMAN) == "Rechteck 2");
    REQUIRE(localize_default_name("Box 3a", "SHAPE_RECTANGLE",
      Language::GERMAN) == "Rechteck");
    REQUIRE(localize_default_name("Artboard \xE2\x80\x93 1",
      "SHAPE_ARTBOARD", Language::GERMAN) == "Zeichenfl\xC3\xA4" "che 1");
    REQUIRE(localize_default_name("Image 7", "SHAPE_IMAGE",
      Language::GERMAN) == "Bild 7");
    REQUIRE(localize_default_name("Repeat Grid 4", "SHAPE_REPEAT_GRID",
      Language::GERMAN) == "Wiederholungsraster 4");
    REQUIRE(localize_default_name("Union 5", "SHAPE_UNION",
      Language::GERMAN) == "Vereinigungsmenge 5");
    REQUIRE(localize_default_name("Scroll Group 6", "SHAPE_SCROLLABLE_GROUP",
      Language::GERMAN) == "Bildlaufgruppe 6");
    REQUIRE(localize_default_name("Symbol 3", "SHAPE_SYMBOL",
      Language::GERMAN) == "Symbol 3");
    REQUIRE(localize_default_name("Flow 3", "FLOW_NAME", Language::GERMAN) ==
      "Flow 3");
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
