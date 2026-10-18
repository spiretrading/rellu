#include <fstream>
#include <iterator>
#include <doctest/doctest.h>
#include "Rellu/Text/FontLibrary.hpp"

using namespace Rellu;

namespace {
  std::shared_ptr<const Typeface> get_arial() {
    static auto library = FontLibrary(get_system_font_directories());
    return library.resolve(Font("Arial", "Regular", "ArialMT", 16)).
      m_typeface;
  }

  double get_width(const std::vector<ShapedGlyph>& glyphs) {
    auto width = 0.0;
    for(auto& glyph : glyphs) {
      width += glyph.m_advance;
    }
    return width;
  }
}

TEST_SUITE("Typeface") {
  TEST_CASE("names") {
    auto typeface = get_arial();
    REQUIRE(typeface);
    auto& names = typeface->get_names();
    REQUIRE(names.m_family == "Arial");
    REQUIRE(names.m_style == "Regular");
    REQUIRE(names.m_postscript_name == "ArialMT");
    REQUIRE(names.m_weight == 400);
    REQUIRE(!names.m_is_italic);
  }

  TEST_CASE("metrics") {
    auto& metrics = get_arial()->get_metrics();
    REQUIRE(metrics.m_ascender > 0.8);
    REQUIRE(metrics.m_descender < -0.1);
    REQUIRE(metrics.m_line_ascent > 0.8);
    REQUIRE(metrics.m_line_descent > 0.1);
    REQUIRE(metrics.m_underline_offset < 0);
    REQUIRE(metrics.m_underline_thickness > 0);
    REQUIRE(metrics.m_strikeout_offset > 0);
  }

  TEST_CASE("shape") {
    auto typeface = get_arial();
    auto text = std::u16string(u"Hi there");
    auto glyphs = typeface->shape(text, 0, text.size());
    REQUIRE(glyphs.size() == text.size());
    for(auto i = std::size_t(0); i != glyphs.size(); ++i) {
      REQUIRE(glyphs[i].m_cluster == i);
      REQUIRE(glyphs[i].m_advance > 0);
    }
    auto span = typeface->shape(text, 3, 5);
    REQUIRE(span.size() == 5);
    REQUIRE(span.front().m_cluster == 3);
  }

  TEST_CASE("kerning") {
    auto typeface = get_arial();
    auto pair = std::u16string(u"AV");
    auto first = std::u16string(u"A");
    auto second = std::u16string(u"V");
    REQUIRE(get_width(typeface->shape(pair, 0, 2)) <
      get_width(typeface->shape(first, 0, 1)) +
      get_width(typeface->shape(second, 0, 1)));
  }

  TEST_CASE("outline") {
    auto typeface = get_arial();
    auto text = std::u16string(u"A ");
    auto glyphs = typeface->shape(text, 0, 2);
    auto& letter = typeface->get_outline(glyphs[0].m_index);
    REQUIRE(!letter.is_empty());
    auto bounds = get_bounds(letter);
    REQUIRE(bounds.m_width > 0.5);
    REQUIRE(bounds.m_width < 1);
    REQUIRE(bounds.get_top() < -0.6);
    REQUIRE(bounds.get_bottom() <= 0.01);
    REQUIRE(typeface->get_outline(glyphs[1].m_index).is_empty());
    REQUIRE(&typeface->get_outline(glyphs[0].m_index) == &letter);
  }

  TEST_CASE("load_from_memory") {
    auto file = std::ifstream(get_arial()->get_path(), std::ios::binary);
    auto data = std::make_shared<std::vector<char>>(
      std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    auto typeface = Typeface::load(data);
    REQUIRE(typeface);
    REQUIRE(typeface->get_path().empty());
    REQUIRE(typeface->get_names().m_postscript_name == "ArialMT");
    REQUIRE(typeface->get_data().size() == data->size());
    REQUIRE(!Typeface::load(data, 1));
  }

  TEST_CASE("glyph_coverage") {
    auto typeface = get_arial();
    REQUIRE(typeface->has_glyph(U'a'));
    REQUIRE(!typeface->has_glyph(U'\U0001F600'));
  }
}
