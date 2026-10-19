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
}
