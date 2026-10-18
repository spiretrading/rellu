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
