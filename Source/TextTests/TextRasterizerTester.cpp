#include <algorithm>
#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include "Rellu/Text/TextRasterizer.hpp"

using namespace Rellu;

namespace {
  const FontLibrary& get_library() {
    static auto library = FontLibrary(get_system_font_directories());
    return library;
  }

  TextLayout make_layout(std::u16string content, Color color) {
    auto text = Text();
    text.m_content = Shared(std::move(content));
    text.m_base_style.m_font = Font("Arial", "Regular", "ArialMT", 16);
    text.m_base_style.m_color = color;
    return lay_out(text, get_library());
  }

  int to_byte(double opacity) {
    return static_cast<int>(std::lround(opacity * 255));
  }

  int get_total(const CoverageMask& mask) {
    auto total = 0;
    for(auto coverage : mask.m_coverage) {
      total += coverage;
    }
    return total;
  }
}

TEST_SUITE("TextRasterizer") {
  TEST_CASE("black_opacity") {
    auto expected = std::array<int, 17>({0, 11, 21, 32, 44, 55, 67, 78, 91,
      103, 116, 130, 145, 161, 180, 204, 255});
    for(auto coverage = 0; coverage <= 16; ++coverage) {
      REQUIRE(to_byte(get_text_opacity(coverage, Color(0, 0, 0, 1))) ==
        expected[coverage]);
    }
  }

  TEST_CASE("color_opacity") {
    REQUIRE(to_byte(get_text_opacity(8, Color(1, 1, 1, 1))) == 188);
    REQUIRE(to_byte(get_text_opacity(8, Color(1, 0, 0, 1))) == 98);
    REQUIRE(to_byte(get_text_opacity(16, Color(1, 0, 0, 1))) == 255);
    REQUIRE(get_text_opacity(0, Color(1, 1, 1, 1)) == 0);
    REQUIRE(get_text_opacity(8, Color(0.1, 0.1, 0.1, 1)) ==
      get_text_opacity(8, Color(0, 0, 0, 1)));
  }

  TEST_CASE("supported_transforms") {
    REQUIRE(TextRasterizer::is_supported(Matrix()));
    REQUIRE(TextRasterizer::is_supported(make_scale(2, 2)));
    REQUIRE(TextRasterizer::is_supported(make_rotation(0.5)));
    REQUIRE(!TextRasterizer::is_supported(make_scale(0, 1)));
  }

  TEST_CASE("outlined_glyphs") {
    auto layout = make_layout(u"H", Color(0, 0, 0, 1));
    auto& glyph = layout.m_glyphs.front();
    REQUIRE(!TextRasterizer::is_outlined(glyph, make_scale(16, 16)));
    REQUIRE(TextRasterizer::is_outlined(glyph, make_scale(17, 17)));
    REQUIRE(TextRasterizer::is_outlined(glyph, make_scale(1, 17)));
    REQUIRE(get_default_text_rasterizer().rasterize(
      layout, make_scale(17, 17)).empty());
  }

  TEST_CASE("rasterize") {
    auto rasterizer = TextRasterizer();
    auto layout = make_layout(u"Hi", Color(0, 0, 0, 1));
    auto rasters = rasterizer.rasterize(layout, make_translation(10, 20));
    REQUIRE(rasters.size() == 1);
    auto& mask = rasters[0].m_mask;
    REQUIRE(rasters[0].m_color == Color(0, 0, 0, 1));
    REQUIRE(mask.m_left >= 10);
    REQUIRE(mask.m_top >= 5);
    REQUIRE(mask.m_top + mask.m_height <= 21);
    REQUIRE(mask.m_coverage.size() ==
      static_cast<std::size_t>(mask.m_width * mask.m_height));
    REQUIRE(*std::max_element(mask.m_coverage.begin(),
      mask.m_coverage.end()) == 16);
    auto doubled = rasterizer.rasterize(layout, make_scale(2, 2));
    REQUIRE(get_total(doubled[0].m_mask) > 3 * get_total(mask));
  }

  TEST_CASE("pixel_snapping") {
    auto rasterizer = TextRasterizer();
    auto layout = make_layout(u"H", Color(0, 0, 0, 1));
    auto base = rasterizer.rasterize(layout, make_translation(0, 20));
    auto across = rasterizer.rasterize(layout, make_translation(0, 20.4));
    REQUIRE(across[0].m_mask.m_coverage == base[0].m_mask.m_coverage);
    REQUIRE(across[0].m_mask.m_top == base[0].m_mask.m_top);
    auto along = rasterizer.rasterize(layout, make_translation(0.1, 20));
    REQUIRE(along[0].m_mask.m_coverage == base[0].m_mask.m_coverage);
    auto quarter = rasterizer.rasterize(layout, make_translation(0.25, 20));
    REQUIRE(quarter[0].m_mask.m_coverage != base[0].m_mask.m_coverage);
  }

  TEST_CASE("overlapping_glyphs") {
    auto layout = make_layout(u"H", Color(0, 0, 0, 1));
    layout.m_glyphs.push_back(layout.m_glyphs.front());
    auto rasters =
      get_default_text_rasterizer().rasterize(layout, make_translation(0, 20));
    REQUIRE(rasters.size() == 2);
    REQUIRE(rasters[0].m_mask.m_coverage == rasters[1].m_mask.m_coverage);
    REQUIRE(rasters[0].m_mask.m_left == rasters[1].m_mask.m_left);
    auto apart = make_layout(u"H H", Color(0, 0, 0, 1));
    REQUIRE(get_default_text_rasterizer().rasterize(
      apart, make_translation(0, 20)).size() == 1);
  }

  TEST_CASE("origin") {
    auto glyph = make_layout(u"H", Color(0, 0, 0, 1)).m_glyphs.front();
    glyph.m_origin = Point(10.3, 20.4);
    REQUIRE(TextRasterizer::get_origin(glyph, Matrix()) == Point(10.25, 20));
    auto vertical = Matrix(0, -1, 1, 0, 0, 0);
    glyph.m_origin = Point(-14.458, 9.3);
    REQUIRE(TextRasterizer::get_origin(glyph, vertical) == Point(9, 15));
    glyph.m_origin = Point(-14.3, 9.6);
    REQUIRE(TextRasterizer::get_origin(glyph, vertical) == Point(10, 14));
    auto rotated = make_rotation(0.5);
    glyph.m_origin = Point(3, 4);
    auto origin = rotated.map(glyph.m_origin);
    auto quarter = [] (double value) {
      return std::floor(value * 4 + 0.5) / 4;
    };
    REQUIRE(TextRasterizer::get_origin(glyph, rotated) ==
      Point(quarter(origin.m_x), quarter(origin.m_y)));
  }

  TEST_CASE("color_glyphs") {
    auto layout = make_layout(u"✅", Color(0, 0, 0, 1));
    REQUIRE(layout.m_glyphs.size() == 1);
    REQUIRE(get_default_text_rasterizer().rasterize(
      layout, make_translation(0, 20)).empty());
  }

  TEST_CASE("colors") {
    auto text = Text();
    text.m_content = Shared(std::u16string(u"ab"));
    text.m_base_style.m_font = Font("Arial", "Regular", "ArialMT", 16);
    text.m_runs.push_back(TextRun(1, text.m_base_style));
    text.m_runs.push_back(TextRun(1, text.m_base_style));
    text.m_runs.back().m_style.m_color = Color(1, 0, 0, 1);
    text.m_runs.back().m_style.m_is_underlined = true;
    auto rasters = get_default_text_rasterizer().rasterize(
      lay_out(text, get_library()), Matrix());
    REQUIRE(rasters.size() == 3);
    REQUIRE(rasters[0].m_color == Color(0, 0, 0, 1));
    REQUIRE(rasters[1].m_color == Color(1, 0, 0, 1));
    REQUIRE(rasters[2].m_color == Color(1, 0, 0, 1));
  }

  TEST_CASE("free_type") {
    auto rasterizer = TextRasterizer(GlyphRasterization::FREE_TYPE);
    REQUIRE(rasterizer.get_rasterization() == GlyphRasterization::FREE_TYPE);
    auto rasters = rasterizer.rasterize(
      make_layout(u"Hi", Color(0, 0, 0, 1)), make_translation(10, 20));
    REQUIRE(rasters.size() == 1);
    REQUIRE(get_total(rasters[0].m_mask) > 0);
  }
}
